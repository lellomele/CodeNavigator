use crate::{
    parser, store,
    telemetry::{self, Log},
};
use anyhow::{Context, Result};
use fs2::FileExt;
use rusqlite::{OptionalExtension, params};
use serde_json::json;
use std::{
    collections::HashMap,
    fs::{self, OpenOptions},
    io::Read,
    path::{Path, PathBuf},
    sync::{Arc, Mutex, atomic::AtomicBool, mpsc},
    time::Instant,
};
use walkdir::WalkDir;

pub struct Options {
    pub root: PathBuf,
    pub db: PathBuf,
    pub parsers: PathBuf,
    pub jobs: usize,
    pub timeout_ms: u64,
    pub cancel_file: Option<PathBuf>,
    pub extensions: Option<Vec<String>>,
}
struct Task {
    path: String,
    source: PathBuf,
    exe: Option<PathBuf>,
    parser_hash: String,
    work: PathBuf,
}
struct Completed {
    task: Task,
    hash: String,
    result: Result<parser::Parsed>,
}
fn digest(bytes: &[u8]) -> String {
    blake3::hash(bytes).to_hex().to_string()
}
fn read_source(path: &Path) -> Result<Vec<u8>> {
    let mut bytes = Vec::new();
    fs::File::open(path)?
        .take(32 * 1024 * 1024 + 1)
        .read_to_end(&mut bytes)?;
    anyhow::ensure!(
        bytes.len() <= 32 * 1024 * 1024,
        "File oltre il limite di 32 MiB"
    );
    Ok(bytes)
}
fn absolute(path: &Path) -> Result<PathBuf> {
    Ok(if path.is_absolute() {
        path.into()
    } else {
        std::env::current_dir()?.join(path)
    })
}

pub fn build(mut options: Options, flag: Arc<AtomicBool>) -> Result<()> {
    options.root = fs::canonicalize(&options.root).context("Cartella progetto non accessibile")?;
    options.parsers = absolute(&options.parsers)?;
    options.db = absolute(&options.db)?;
    let base = options
        .db
        .parent()
        .context("Percorso database non valido")?;
    fs::create_dir_all(base)?;
    let lock = OpenOptions::new()
        .create(true)
        .truncate(false)
        .read(true)
        .write(true)
        .open(options.db.with_extension("lock"))?;
    lock.try_lock_exclusive()
        .context("Un'altra indicizzazione sta usando questo database")?;
    let run = format!("{}-{}", telemetry::now(), std::process::id());
    let mut log = Log::new(&base.join("logs"), &run)?;
    let start = Instant::now();
    let result = build_inner(&options, flag, &run, &mut log);
    if let Err(e) = &result {
        let _=log.event("failed",json!({"error":format!("{e:#}"),"backtrace":e.backtrace().to_string(),"elapsed_ms":start.elapsed().as_millis()}));
    }
    let _ = log.sync();
    // The OS releases this lock even when the process is forcibly terminated.
    drop(lock);
    result
}

fn build_inner(o: &Options, flag: Arc<AtomicBool>, run: &str, log: &mut Log) -> Result<()> {
    let mut conn = store::write(&o.db)?;
    let root = o.root.to_string_lossy().to_string();
    let previous: Option<String> = conn
        .query_row("SELECT value FROM meta WHERE key='root'", [], |r| r.get(0))
        .optional()?;
    if let Some(previous) = previous {
        anyhow::ensure!(
            previous == root,
            "Il database appartiene a un'altra cartella"
        )
    }
    let work = o.db.parent().unwrap().join("work").join(run);
    fs::create_dir_all(&work)?;
    let interrupted = conn.execute(
        "UPDATE runs SET status='interrupted' WHERE status='running'",
        [],
    )?;
    conn.execute_batch(
        "DELETE FROM pending_files; DELETE FROM pending_symbols; DELETE FROM pending_xrefs; DELETE FROM scanned;",
    )?;
    conn.execute(
        "INSERT INTO runs(id,started,status) VALUES(?1,?2,'running')",
        params![run, telemetry::now() as i64],
    )?;
    log.event("started",json!({"root":root,"jobs":o.jobs,"timeout_ms":o.timeout_ms,"recovered_runs":interrupted,"parsers":o.parsers,"platform":std::env::consts::OS,"architecture":std::env::consts::ARCH}))?;
    telemetry::emit(&json!({"event":"started","run_id":run,"recovered_runs":interrupted}));
    let mut fingerprints = HashMap::new();
    let mut tasks = Vec::new();
    let mut unchanged = 0;
    let mut total = 0;
    let iterator = WalkDir::new(&o.root)
        .follow_links(false)
        .into_iter()
        .filter_entry(crate::discovery::include);
    // A failed directory enumeration aborts publication; unreadable directories never look deleted.
    let tx = conn.transaction()?;
    for entry in iterator {
        anyhow::ensure!(
            !parser::canceled(&flag, &o.cancel_file),
            "Indicizzazione annullata durante la scansione"
        );
        let entry = entry.context("Scansione incompleta: indice precedente conservato")?;
        if !entry.file_type().is_file() {
            continue;
        }
        let source = entry.path();
        let ext = crate::discovery::extension(source);
        if !o
            .extensions
            .as_ref()
            .map(|xs| xs.contains(&ext))
            .unwrap_or_else(|| crate::discovery::classic(&ext))
        {
            continue;
        }
        let name = parser::executable(source).unwrap_or("text");
        let path = source
            .strip_prefix(&o.root)?
            .to_str()
            .context("Nome file non Unicode")?
            .replace('\\', "/");
        tx.execute("INSERT INTO scanned(path) VALUES(?1)", [&path])?;
        total += 1;
        let exe = if name == "text" {
            None
        } else {
            Some(parser::binary(&o.parsers, name))
        };
        let fingerprint = fingerprints
            .entry(name)
            .or_insert_with(|| {
                exe.as_ref()
                    .map(|p| {
                        fs::read(p)
                            .map(|b| digest(&b))
                            .unwrap_or_else(|_| "missing".into())
                    })
                    .unwrap_or_else(|| "text-v1".into())
            })
            .clone()
            + "-xref-v2";
        let metadata = entry.metadata()?;
        let bytes = if metadata.len() <= 32 * 1024 * 1024 {
            read_source(source).ok()
        } else {
            None
        };
        if bytes
            .as_ref()
            .is_some_and(|b| crate::textfile::Text::decode(b).is_err())
        {
            tx.execute("DELETE FROM scanned WHERE path=?1", [&path])?;
            total -= 1;
            continue;
        }
        let hash = bytes.as_ref().map(|b| digest(b)).unwrap_or_default();
        let cache: Option<(String, String, String)> = tx
            .query_row(
                "SELECT hash,parser,status FROM files WHERE path=?1",
                [&path],
                |r| Ok((r.get(0)?, r.get(1)?, r.get(2)?)),
            )
            .optional()?;
        if !hash.is_empty()
            && cache.is_some_and(|(h, p, s)| h == hash && p == fingerprint && s == "ready")
        {
            unchanged += 1;
            continue;
        }
        tasks.push(Task {
            path,
            source: source.into(),
            exe,
            parser_hash: fingerprint,
            work: work.join(tasks.len().to_string()),
        });
    }
    tx.commit()?;
    log.event("scan",json!({"files":total,"unchanged":unchanged,"scheduled":tasks.len(),"parser_fingerprints":fingerprints}))?;
    telemetry::emit(
        &json!({"event":"scan","files":total,"unchanged":unchanged,"scheduled":tasks.len()}),
    );
    let scheduled = tasks.len();
    let queue = Arc::new(Mutex::new(tasks.into_iter()));
    let (send, receive) = mpsc::sync_channel::<Completed>(o.jobs * 2);
    let mut errors = 0;
    let mut done: usize = 0;
    let mut parsed_symbols = 0;
    let stage_result = std::thread::scope(|scope| -> Result<()> {
        for _ in 0..o.jobs {
            let send = send.clone();
            let queue = queue.clone();
            let flag = flag.clone();
            let cancel = o.cancel_file.clone();
            let timeout = o.timeout_ms;
            scope.spawn(move || {
                loop {
                    if parser::canceled(&flag, &cancel) {
                        break;
                    }
                    let Some(task) = queue.lock().unwrap().next() else {
                        break;
                    };
                    let mut hash = String::new();
                    let result = (|| -> Result<parser::Parsed> {
                        anyhow::ensure!(
                            fs::metadata(&task.source)?.len() <= 32 * 1024 * 1024,
                            "File oltre il limite di 32 MiB"
                        );
                        let bytes = read_source(&task.source)?;
                        hash = digest(&bytes);
                        let text = crate::textfile::Text::decode(&bytes)?;
                        let ext = task
                            .source
                            .extension()
                            .and_then(|s| s.to_str())
                            .unwrap_or("txt");
                        let mut parsed = if let Some(exe) = &task.exe {
                            parser::run(
                                exe,
                                text.text.as_bytes(),
                                ext,
                                &task.work,
                                timeout,
                                flag.clone(),
                                &cancel,
                            )?
                        } else {
                            parser::Parsed {
                                xrefs: Vec::new(),
                                symbols: Vec::new(),
                                stderr: String::new(),
                                elapsed_ms: 0,
                            }
                        };
                        parsed.xrefs = crate::xref::run(
                            &text.text,
                            &ext.to_ascii_lowercase(),
                            &task.work,
                            timeout,
                            &flag,
                            &cancel,
                        )?;
                        let mut declarations: std::collections::HashSet<_> = parsed
                            .xrefs
                            .iter()
                            .filter(|e| e.kind == "declaration")
                            .map(|e| (e.target.clone(), e.line))
                            .collect();
                        for symbol in &parsed.symbols {
                            if symbol.kind != "include"
                                && declarations.insert((symbol.name.clone(), symbol.line as usize))
                            {
                                parsed.xrefs.push(crate::xref::Edge {
                                    source: symbol.scope.clone(),
                                    target: symbol.name.clone(),
                                    kind: "declaration".into(),
                                    line: symbol.line as usize,
                                    column: symbol.column as usize,
                                });
                            }
                        }
                        anyhow::ensure!(
                            digest(&read_source(&task.source)?) == hash,
                            "File modificato durante l'analisi: riprovare"
                        );
                        Ok(parsed)
                    })();
                    if send.send(Completed { task, hash, result }).is_err() {
                        break;
                    }
                }
            });
        }
        drop(send);
        // Staging is disposable after interruption. Batch its commits while keeping
        // synchronous=FULL and the final publication transaction unchanged.
        let mut tx = conn.transaction()?;
        for item in receive {
            done += 1;
            match item.result {
                Ok(parsed) => {
                    parsed_symbols += parsed.symbols.len();
                    tx.execute(
                        "INSERT INTO pending_files VALUES(?1,?2,?3,'ready','')",
                        params![item.task.path, item.hash, item.task.parser_hash],
                    )?;
                    {
                        let mut insert = tx.prepare_cached(
                            "INSERT INTO pending_symbols VALUES(?1,?2,?3,?4,?5,?6,?7)",
                        )?;
                        for s in &parsed.symbols {
                            insert.execute(params![
                                item.task.path,
                                s.name,
                                s.scope,
                                s.kind,
                                s.line,
                                s.column,
                                s.detail
                            ])?;
                        }
                    }
                    for edge in &parsed.xrefs {
                        tx.execute(
                            "INSERT INTO pending_xrefs VALUES(?1,?2,?3,?4,?5,?6)",
                            params![
                                item.task.path,
                                edge.source,
                                edge.target,
                                edge.kind,
                                edge.line,
                                edge.column
                            ],
                        )?;
                    }
                    log.event("parsed",json!({"path":item.task.path,"hash":item.hash,"symbols":parsed.symbols.len(),"elapsed_ms":parsed.elapsed_ms,"stderr":parsed.stderr}))?;
                    // Inputs and successful parser output are transient; failures retain their diagnostic output.
                    if item.task.work.exists() {
                        for entry in fs::read_dir(&item.task.work)? {
                            let p = entry?.path();
                            if p.is_file() {
                                fs::remove_file(p)?;
                            }
                        }
                        fs::remove_dir(&item.task.work)?;
                    }
                }
                Err(e) => {
                    errors += 1;
                    let error = format!("{e:#}");
                    tx.execute(
                        "INSERT INTO pending_files VALUES(?1,?2,?3,'stale',?4)",
                        params![item.task.path, item.hash, item.task.parser_hash, error],
                    )?;
                    log.event("parser_error",json!({"path":item.task.path,"hash":item.hash,"error":error,"diagnostics":item.task.work}))?;
                    // The original source is never retained in the diagnostic bundle.
                    if let Some(ext) = item.task.source.extension().and_then(|s| s.to_str()) {
                        let _ = fs::remove_file(item.task.work.join(format!("input.{ext}")));
                    }
                }
            }
            if done.is_multiple_of(64) {
                tx.commit()?;
                tx = conn.transaction()?;
            }
            telemetry::emit(
                &json!({"event":"progress","done":done,"total":scheduled,"errors":errors,"path":item.task.path}),
            );
        }
        tx.commit()?;
        Ok(())
    });
    stage_result?;
    if parser::canceled(&flag, &o.cancel_file) {
        conn.execute(
            "UPDATE runs SET status='canceled',finished=?2 WHERE id=?1",
            params![run, telemetry::now() as i64],
        )?;
        log.event("canceled", json!({"done":done,"scheduled":scheduled}))?;
        telemetry::emit(&json!({"event":"canceled","published":false}));
        return Ok(());
    }
    anyhow::ensure!(
        done == scheduled,
        "Alcuni processi non hanno restituito risultati"
    );
    // One transaction publishes the entire scan, including removals. WAL readers retain their snapshot.
    let tx = conn.transaction()?;
    tx.execute(
        "DELETE FROM files WHERE path NOT IN (SELECT path FROM scanned)",
        [],
    )?;
    tx.execute(
        "DELETE FROM symbols WHERE path IN (SELECT path FROM pending_files WHERE status='ready')",
        [],
    )?;
    tx.execute("INSERT INTO files SELECT * FROM pending_files WHERE true ON CONFLICT(path) DO UPDATE SET hash=CASE WHEN excluded.status='ready' THEN excluded.hash ELSE files.hash END,parser=CASE WHEN excluded.status='ready' THEN excluded.parser ELSE files.parser END,status=excluded.status,error=excluded.error",[])?;
    tx.execute(
        "DELETE FROM xrefs WHERE path IN (SELECT path FROM pending_files WHERE status='ready')",
        [],
    )?;
    tx.execute("INSERT INTO xrefs SELECT * FROM pending_xrefs", [])?;
    tx.execute(
        "INSERT INTO symbols(path,name,scope,kind,line,col,detail) SELECT * FROM pending_symbols",
        [],
    )?;
    tx.execute("UPDATE symbols SET ascii_only=1 WHERE path IN (SELECT path FROM pending_files WHERE status='ready') AND name NOT GLOB '*[^ -~]*' AND scope NOT GLOB '*[^ -~]*'",[])?;
    tx.execute("INSERT OR REPLACE INTO meta VALUES('root',?1)", [&root])?;
    tx.execute("INSERT OR REPLACE INTO meta VALUES('generation',?1)", [run])?;
    let summary = json!({"event":"completed","files":total,"parsed":done-errors,"unchanged":unchanged,"errors":errors,"parsed_symbols":parsed_symbols,"run_id":run,"xref":"syntactic"});
    tx.execute(
        "UPDATE runs SET status=?2,finished=?3,summary=?4 WHERE id=?1",
        params![
            run,
            if errors == 0 { "complete" } else { "partial" },
            telemetry::now() as i64,
            summary.to_string()
        ],
    )?;
    tx.execute_batch(
        "DELETE FROM pending_files; DELETE FROM pending_symbols; DELETE FROM pending_xrefs; DELETE FROM scanned;",
    )?;
    tx.commit()?;
    log.event("published", summary.clone())?;
    log.sync()?;
    telemetry::emit(&summary);
    Ok(())
}

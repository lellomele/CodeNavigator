use crate::{discovery, search, store, telemetry};
use anyhow::{Context, Result};
use fs2::FileExt;
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::{
    fs::{self, OpenOptions},
    io::Write,
    path::{Path, PathBuf},
};

#[derive(Deserialize)]
pub struct Request {
    pub db: PathBuf,
    pub pattern: String,
    pub replacement: String,
    pub mode: String,
    #[serde(default)]
    pub case_sensitive: bool,
    #[serde(default)]
    pub whole_words: bool,
    #[serde(default)]
    pub preview_only: bool,
    pub files: Vec<String>,
}
#[derive(Serialize, Deserialize)]
struct Entry {
    path: String,
    before: String,
    after: String,
    count: usize,
    state: String,
    preview: Vec<Value>,
}
#[derive(Serialize, Deserialize)]
struct Journal {
    root: PathBuf,
    db: PathBuf,
    state: String,
    entries: Vec<Entry>,
}
fn hash(b: &[u8]) -> String {
    blake3::hash(b).to_hex().to_string()
}
fn checked(root: &Path, rel: &str) -> Result<PathBuf> {
    let p = Path::new(rel);
    anyhow::ensure!(
        !p.is_absolute()
            && p.components()
                .all(|x| matches!(x, std::path::Component::Normal(_))),
        "Percorso non relativo"
    );
    anyhow::ensure!(
        !p.components().any(|x| x.as_os_str() == ".sn-index"),
        "Percorso riservato"
    );
    let path = fs::canonicalize(root.join(p))?;
    anyhow::ensure!(
        path.starts_with(root) && path.is_file(),
        "File fuori dal progetto"
    );
    Ok(path)
}
fn lock(db: &Path) -> Result<fs::File> {
    let f = OpenOptions::new()
        .create(true)
        .truncate(false)
        .read(true)
        .write(true)
        .open(db.with_extension("lock"))?;
    f.try_lock_exclusive()
        .context("Operazione già in corso sul progetto")?;
    Ok(f)
}
fn durable(path: &Path, b: &[u8]) -> Result<()> {
    let mut f = OpenOptions::new().write(true).create_new(true).open(path)?;
    f.write_all(b)?;
    f.sync_all()?;
    Ok(())
}
fn atomic(path: &Path, bytes: &[u8]) -> Result<()> {
    let mut tmp =
        tempfile::NamedTempFile::new_in(path.parent().context("Percorso senza cartella")?)?;
    tmp.write_all(bytes)?;
    tmp.as_file().sync_all()?;
    let tmp = tmp.into_temp_path();
    if path.exists() {
        #[cfg(windows)]
        {
            use std::os::windows::ffi::OsStrExt;
            use windows_sys::Win32::Storage::FileSystem::ReplaceFileW;
            let a: Vec<u16> = path.as_os_str().encode_wide().chain(Some(0)).collect();
            let b: Vec<u16> = tmp.as_os_str().encode_wide().chain(Some(0)).collect();
            let result = unsafe {
                ReplaceFileW(
                    a.as_ptr(),
                    b.as_ptr(),
                    std::ptr::null(),
                    0,
                    std::ptr::null(),
                    std::ptr::null(),
                )
            };
            anyhow::ensure!(
                result != 0,
                "Scrittura atomica: {}",
                std::io::Error::last_os_error()
            );
        }
        #[cfg(not(windows))]
        {
            fs::set_permissions(&tmp, fs::metadata(path)?.permissions())?;
            tmp.persist(path)?;
        }
    } else {
        tmp.persist_noclobber(path)?;
    }
    Ok(())
}
fn save(dir: &Path, j: &Journal) -> Result<()> {
    atomic(&dir.join("journal.json"), &serde_json::to_vec_pretty(j)?)
}
pub fn plan(request: &Path) -> Result<Value> {
    let r: Request = serde_json::from_slice(&fs::read(request)?)?;
    anyhow::ensure!(!r.pattern.is_empty(), "Specificare il testo da cercare");
    anyhow::ensure!(
        !r.files.is_empty() && r.files.len() <= 10000,
        "Selezionare da 1 a 10000 file"
    );
    anyhow::ensure!(
        r.replacement.len() <= 65536,
        "Testo sostitutivo troppo lungo"
    );
    let _lock = lock(&r.db)?;
    let conn = store::read(&r.db)?;
    let root: String =
        conn.query_row("SELECT value FROM meta WHERE key='root'", [], |r| r.get(0))?;
    let root = fs::canonicalize(root)?;
    let regex = search::text_matcher(&r.pattern, &r.mode, r.case_sensitive, r.whole_words)?;
    if r.mode == "regex" {
        validate_captures(&regex, &r.replacement)?;
    }
    let dir = r.db.parent().unwrap().join("changes").join(format!(
        "{}-{}",
        telemetry::now(),
        std::process::id()
    ));
    if !r.preview_only {
        fs::create_dir_all(&dir)?;
    }
    let mut journal = Journal {
        root,
        db: fs::canonicalize(&r.db)?,
        state: "planned".into(),
        entries: Vec::new(),
    };
    let mut used = std::collections::HashSet::new();
    let mut total_bytes = 0;
    for file in &r.files {
        anyhow::ensure!(used.insert(file.clone()), "File duplicato");
        let exists: bool = conn.query_row(
            "SELECT EXISTS(SELECT 1 FROM files WHERE path=?1)",
            [file],
            |r| r.get(0),
        )?;
        anyhow::ensure!(exists, "File non incluso nell'indice: {file}");
        let path = checked(&journal.root, file)?;
        let original = discovery::read_text(&path)?;
        let document = crate::textfile::Text::decode(&original)?;
        let text = &document.text;
        let mut changed = String::new();
        let mut count = 0;
        let mut preview = Vec::new();
        // Search and replacement deliberately share the same per-line semantics.
        for (line, segment) in text.split_inclusive('\n').enumerate() {
            let body = segment.trim_end_matches(['\r', '\n']);
            let ending = &segment[body.len()..];
            let hits = regex.find_iter(body).count();
            anyhow::ensure!(count + hits <= 100_000, "Troppe corrispondenze in {file}");
            let mut replacement = String::new();
            let mut last = 0;
            for caps in regex.captures_iter(body) {
                let hit = caps.get(0).unwrap();
                replacement.push_str(&body[last..hit.start()]);
                if r.mode == "regex" {
                    caps.expand(&r.replacement, &mut replacement);
                } else {
                    replacement.push_str(&r.replacement);
                }
                anyhow::ensure!(
                    replacement.len() as u64 <= discovery::LIMIT,
                    "Risultato troppo grande"
                );
                last = hit.end();
            }
            replacement.push_str(&body[last..]);
            if hits > 0 && preview.len() < 200 {
                preview.push(json!({"line":line+1,"before":body.chars().take(500).collect::<String>(),"after":replacement.chars().take(500).collect::<String>()}));
            }
            count += hits;
            changed.push_str(&replacement);
            changed.push_str(ending);
            anyhow::ensure!(
                changed.len() as u64 <= discovery::LIMIT,
                "Risultato oltre 32 MiB"
            );
        }
        let changed = document.encode(&changed)?;
        crate::textfile::Text::decode(&changed)
            .context("La sostituzione produrrebbe un file non testuale")?;
        anyhow::ensure!(
            changed.len() as u64 <= discovery::LIMIT,
            "File codificato oltre 32 MiB"
        );
        if count == 0 || (!r.preview_only && original == changed) {
            continue;
        }
        total_bytes += original.len() + changed.len();
        anyhow::ensure!(
            total_bytes <= 256 * 1024 * 1024,
            "Operazione oltre 256 MiB: ridurre la selezione"
        );
        let id = journal.entries.len();
        if !r.preview_only {
            durable(&dir.join(format!("{id}.before")), &original)?;
            durable(&dir.join(format!("{id}.after")), &changed)?;
        }
        journal.entries.push(Entry {
            path: file.clone(),
            before: hash(&original),
            after: hash(&changed),
            count,
            state: "planned".into(),
            preview,
        });
    }
    if r.preview_only {
        return Ok(json!({"event":"print_preview","entries":journal.entries}));
    }
    save(&dir, &journal)?;
    Ok(
        json!({"event":"replacement_plan","journal":dir.join("journal.json"),"entries":journal.entries}),
    )
}
fn validate_captures(regex: &regex::Regex, value: &str) -> Result<()> {
    let names: std::collections::HashSet<_> = regex.capture_names().flatten().collect();
    let chars = value.as_bytes();
    let mut i = 0;
    while i < chars.len() {
        if chars[i] != b'$' {
            i += 1;
            continue;
        }
        i += 1;
        if i < chars.len() && chars[i] == b'$' {
            i += 1;
            continue;
        }
        let brace = i < chars.len() && chars[i] == b'{';
        if brace {
            i += 1;
        }
        let start = i;
        while i < chars.len() && (chars[i].is_ascii_alphanumeric() || chars[i] == b'_') {
            i += 1;
        }
        if start == i {
            continue;
        }
        let name = &value[start..i];
        anyhow::ensure!(
            name.parse::<usize>()
                .is_ok_and(|n| n < regex.captures_len())
                || names.contains(name),
            "Gruppo di sostituzione inesistente: {name}"
        );
        if brace {
            anyhow::ensure!(i < chars.len() && chars[i] == b'}', "Gruppo non chiuso");
            i += 1;
        }
    }
    Ok(())
}
pub fn execute(journal_path: &Path, undo: bool) -> Result<Value> {
    let path = fs::canonicalize(journal_path)?;
    let dir = path.parent().unwrap();
    let mut j: Journal = serde_json::from_slice(&fs::read(&path)?)?;
    anyhow::ensure!(
        dir.parent() == Some(fs::canonicalize(j.db.parent().unwrap().join("changes"))?.as_path()),
        "Registro fuori dalla cartella del progetto"
    );
    let _lock = lock(&j.db)?;
    let mut log = telemetry::Log::new(
        &j.db.parent().unwrap().join("logs"),
        &format!("replace-{}-{}", telemetry::now(), std::process::id()),
    )?;
    if !undo {
        anyhow::ensure!(
            j.state == "planned",
            "Operazione già avviata: usare Ripristina"
        );
    }
    // Validate every file and backup before changing the first one.
    for (i, e) in j.entries.iter().enumerate() {
        let p = checked(&j.root, &e.path)?;
        let current = hash(&discovery::read_text(&p)?);
        anyhow::ensure!(
            if undo {
                current == e.before || current == e.after
            } else {
                current == e.before
            },
            "Conflitto esterno: {}",
            e.path
        );
        for (suffix, expected) in [("before", &e.before), ("after", &e.after)] {
            anyhow::ensure!(
                hash(&fs::read(dir.join(format!("{i}.{suffix}")))?) == *expected,
                "Copia di recupero danneggiata"
            );
        }
    }
    j.state = if undo { "restoring" } else { "applying" }.into();
    save(dir, &j)?;
    for i in 0..j.entries.len() {
        let e = &j.entries[i];
        let p = checked(&j.root, &e.path)?;
        let current = hash(&discovery::read_text(&p)?);
        let expected = if undo { &e.after } else { &e.before };
        let target = if undo { &e.before } else { &e.after };
        if current != *target {
            anyhow::ensure!(
                current == *expected,
                "File cambiato durante l'operazione: {}",
                e.path
            );
            let b = fs::read(dir.join(format!("{i}.{}", if undo { "before" } else { "after" })))?;
            log.event(
                "file_write_started",
                json!({"path":e.path,"before":current,"after":target,"undo":undo}),
            )?;
            atomic(&p, &b)?;
        }
        j.entries[i].state = if undo { "restored" } else { "applied" }.into();
        save(dir, &j)?;
        log.event(
            "file_write_completed",
            json!({"path":j.entries[i].path,"state":j.entries[i].state}),
        )?;
    }
    j.state = if undo { "restored" } else { "applied" }.into();
    save(dir, &j)?;
    log.sync()?;
    Ok(
        json!({"event":"replacement_completed","state":j.state,"files":j.entries.len(),"journal":path}),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn reject_unknown_capture() {
        let r = regex::Regex::new("(a)").unwrap();
        assert!(validate_captures(&r, "$2").is_err());
        assert!(validate_captures(&r, "${1}$$").is_ok());
    }
    #[test]
    fn reject_escape() {
        let d = tempfile::tempdir().unwrap();
        assert!(checked(d.path(), "../secret").is_err());
        assert!(checked(d.path(), ".sn-index/file").is_err());
    }
    #[test]
    fn atomic_write_replaces_whole_file() {
        let d = tempfile::tempdir().unwrap();
        let p = d.path().join("a");
        atomic(&p, b"first").unwrap();
        atomic(&p, b"second").unwrap();
        assert_eq!(fs::read(p).unwrap(), b"second");
    }
}

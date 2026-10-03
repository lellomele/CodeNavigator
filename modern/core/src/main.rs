mod discovery;
mod file_search;
mod index;
mod parser;
mod replace;
mod search;
mod store;
mod telemetry;
mod textfile;
mod xref;

use anyhow::{Context, Result};
use clap::{Parser, Subcommand};
use serde_json::json;
use std::{
    path::PathBuf,
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    },
};

#[derive(Parser)]
#[command(
    version,
    about = "Code Navigator: indice locale, ricerca dei file e dei simboli"
)]
struct Args {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    Discover {
        #[arg(long)]
        root: PathBuf,
    },
    ReplacePlan {
        #[arg(long)]
        request: PathBuf,
    },
    ReplaceApply {
        #[arg(long)]
        journal: PathBuf,
    },
    ReplaceUndo {
        #[arg(long)]
        journal: PathBuf,
    },
    Index {
        #[arg(long)]
        root: PathBuf,
        #[arg(long)]
        db: PathBuf,
        #[arg(long)]
        parsers: PathBuf,
        #[arg(long, default_value_t = 4)]
        jobs: usize,
        #[arg(long, default_value_t = 15000)]
        timeout_ms: u64,
        #[arg(long)]
        cancel_file: Option<PathBuf>,
        #[arg(long, value_delimiter = ',')]
        extensions: Option<Vec<String>>,
    },
    #[command(hide = true)]
    XrefWorker {
        #[arg(long)]
        input: PathBuf,
        #[arg(long)]
        extension: String,
    },
    Xref {
        #[arg(long)]
        db: PathBuf,
        #[arg(long, default_value = "")]
        subject: String,
        #[arg(long, default_value = "all")]
        relation: String,
        #[arg(long, default_value = "incoming")]
        direction: String,
        #[arg(long, default_value = "")]
        path: String,
    },
    Query {
        #[arg(long)]
        db: PathBuf,
        #[arg(long, default_value = "")]
        pattern: String,
        #[arg(long, default_value = "literal")]
        mode: String,
        #[arg(long, default_value = "")]
        path: String,
        #[arg(long, default_value = "")]
        kind: String,
        #[arg(long)]
        case_sensitive: bool,
        #[arg(long)]
        whole_words: bool,
        #[arg(long, default_value_t = 500)]
        limit: usize,
    },
    Status {
        #[arg(long)]
        db: PathBuf,
    },
    Grep {
        #[arg(long)]
        db: PathBuf,
        #[arg(long, default_value = "")]
        pattern: String,
        #[arg(long, default_value = "literal")]
        mode: String,
        #[arg(long, default_value = "")]
        path: String,
        #[arg(long)]
        case_sensitive: bool,
        #[arg(long)]
        whole_words: bool,
        #[arg(long, default_value_t = 500)]
        limit: usize,
    },
    FindFiles {
        #[arg(long)]
        root: PathBuf,
        #[arg(long, default_value = "*")]
        pattern: String,
        #[arg(long, default_value = "glob")]
        mode: String,
        #[arg(long, default_value = "")]
        path: String,
        #[arg(long)]
        case_sensitive: bool,
        #[arg(long = "exclude-extension")]
        excluded_extensions: Vec<String>,
        #[arg(long, default_value_t = 5000)]
        limit: usize,
        #[arg(long, default_value_t = 5000)]
        timeout_ms: u64,
    },
    Files {
        #[arg(long)]
        db: PathBuf,
    },
    Check {
        #[arg(long)]
        db: PathBuf,
    },
}

fn run() -> Result<()> {
    let args = Args::parse();
    let canceled = Arc::new(AtomicBool::new(false));
    let signal = canceled.clone();
    ctrlc::set_handler(move || {
        signal.store(true, Ordering::Relaxed);
    })?;
    match args.command {
        Command::XrefWorker { input, extension } => {
            use std::io::Read;
            let mut bytes = Vec::new();
            std::fs::File::open(input)?
                .take(32 * 1024 * 1024 + 1)
                .read_to_end(&mut bytes)?;
            anyhow::ensure!(
                bytes.len() <= 32 * 1024 * 1024,
                "Cross-reference input limit exceeded"
            );
            telemetry::emit(&serde_json::to_value(xref::analyze(
                std::str::from_utf8(&bytes)?,
                &extension,
            )?)?);
        }
        Command::Xref {
            db,
            subject,
            relation,
            direction,
            path,
        } => telemetry::emit(&xref::query(
            &store::read(&db)?,
            &subject,
            &relation,
            &direction,
            &path,
        )?),
        Command::Discover { root } => telemetry::emit(&discovery::scan(&root)?),
        Command::FindFiles {
            root,
            pattern,
            mode,
            path,
            case_sensitive,
            excluded_extensions,
            limit,
            timeout_ms,
        } => {
            telemetry::emit(&file_search::find(
                file_search::Options {
                    root: &root,
                    pattern: &pattern,
                    mode: &mode,
                    path: &path,
                    case_sensitive,
                    excluded_extensions: &excluded_extensions,
                    limit: limit.clamp(1, 10000),
                    timeout_ms: timeout_ms.clamp(100, 30000),
                },
                &canceled,
            )?);
        }
        Command::ReplacePlan { request } => telemetry::emit(&replace::plan(&request)?),
        Command::ReplaceApply { journal } => telemetry::emit(&replace::execute(&journal, false)?),
        Command::ReplaceUndo { journal } => telemetry::emit(&replace::execute(&journal, true)?),
        Command::Index {
            root,
            db,
            parsers,
            jobs,
            timeout_ms,
            cancel_file,
            extensions,
        } => {
            index::build(
                index::Options {
                    root,
                    db,
                    parsers,
                    jobs: jobs.clamp(1, 16),
                    timeout_ms: timeout_ms.clamp(100, 300_000),
                    cancel_file,
                    extensions,
                },
                canceled,
            )?;
        }
        Command::Query {
            db,
            pattern,
            mode,
            path,
            kind,
            case_sensitive,
            whole_words,
            limit,
        } => {
            let conn = store::read(&db)?;
            telemetry::emit(&search::query(
                &conn,
                &pattern,
                &mode,
                &path,
                &kind,
                search::MatchOptions {
                    case_sensitive,
                    whole_words,
                },
                limit.clamp(1, 10000),
            )?);
        }
        Command::Status { db } => {
            telemetry::emit(&store::status(&store::read(&db)?)?);
        }
        Command::Grep {
            db,
            pattern,
            mode,
            path,
            case_sensitive,
            whole_words,
            limit,
        } => {
            telemetry::emit(&search::source_text(
                &store::read(&db)?,
                &pattern,
                &mode,
                &path,
                search::MatchOptions {
                    case_sensitive,
                    whole_words,
                },
                limit.clamp(1, 10000),
            )?);
        }
        Command::Files { db } => {
            let conn = store::read(&db)?;
            let mut q = conn.prepare("SELECT path,status,error FROM files ORDER BY path")?;
            let rows = q.query_map([], |r| Ok(json!({"path": r.get::<_,String>(0)?, "status":r.get::<_,String>(1)?, "error":r.get::<_,String>(2)?})))?.collect::<rusqlite::Result<Vec<_>>>()?;
            telemetry::emit(&json!({"event":"files","files":rows}));
        }
        Command::Check { db } => {
            let conn = store::read(&db)?;
            let result: String = conn.query_row("PRAGMA quick_check", [], |r| r.get(0))?;
            telemetry::emit(&json!({"event":"check","result":result}));
            anyhow::ensure!(result == "ok", "Verifica database: {result}");
        }
    }
    Ok(())
}

fn main() {
    std::panic::set_hook(Box::new(|p| {
        let event = json!({"event":"panic","version":env!("CARGO_PKG_VERSION"),"detail":p.to_string(),"backtrace":std::backtrace::Backtrace::force_capture().to_string()});
        eprintln!("{event}");
    }));
    if let Err(e) = run().context("Operazione non completata") {
        eprintln!(
            "{}",
            json!({"event":"error","version":env!("CARGO_PKG_VERSION"),"message":format!("{e:#}")})
        );
        std::process::exit(1);
    }
}

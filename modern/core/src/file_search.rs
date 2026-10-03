use anyhow::{Context, Result};
use serde_json::{Value, json};
use std::{
    collections::BTreeSet,
    fs,
    path::Path,
    sync::atomic::{AtomicBool, Ordering},
    time::{Duration, Instant},
};
use walkdir::WalkDir;

pub struct Options<'a> {
    pub root: &'a Path,
    pub pattern: &'a str,
    pub mode: &'a str,
    pub path: &'a str,
    pub case_sensitive: bool,
    pub excluded_extensions: &'a [String],
    pub limit: usize,
    pub timeout_ms: u64,
}

// File discovery only inspects directory entries. It is independent of the text index.
pub fn find(options: Options<'_>, canceled: &AtomicBool) -> Result<Value> {
    let start = Instant::now();
    let normalized;
    let pattern = if options.mode == "regex" {
        options.pattern
    } else {
        normalized = options.pattern.replace('\\', "/");
        &normalized
    };
    let names = crate::search::matcher(pattern, options.mode, options.case_sensitive)?;
    let paths = crate::search::matcher(
        if options.path.is_empty() {
            "*"
        } else {
            options.path
        },
        "glob",
        false,
    )?;
    let match_relative_path = pattern.contains('/');
    let excluded: BTreeSet<String> = options
        .excluded_extensions
        .iter()
        .map(|ext| ext.to_lowercase())
        .collect();
    let root = fs::canonicalize(options.root).context("Directory del progetto non accessibile")?;
    anyhow::ensure!(
        root.is_dir(),
        "La radice del progetto deve essere una directory"
    );
    let mut results = Vec::new();
    let mut errors = Vec::new();
    let mut skipped = 0;
    let mut scanned = 0;
    let mut truncated = false;
    let mut timed_out = false;
    let mut was_canceled = false;
    // Generated directories remain searchable; only repository/index metadata are pruned.
    for entry in WalkDir::new(&root)
        .follow_links(false)
        .sort_by_file_name()
        .into_iter()
        .filter_entry(|entry| {
            entry.depth() == 0
                || ![".git", ".svn", ".sn-index"]
                    .contains(&entry.file_name().to_string_lossy().as_ref())
        })
    {
        if canceled.load(Ordering::Relaxed) {
            truncated = true;
            was_canceled = true;
            break;
        }
        if start.elapsed() >= Duration::from_millis(options.timeout_ms) {
            truncated = true;
            timed_out = true;
            break;
        }
        let entry = match entry {
            Ok(entry) => entry,
            Err(error) => {
                skipped += 1;
                if errors.len() < 128 {
                    errors.push(json!({"path":error.path().map(|p| p.to_string_lossy()),"message":error.to_string()}));
                }
                continue;
            }
        };
        if !entry.file_type().is_file() {
            continue;
        }
        scanned += 1;
        if excluded.contains(&crate::discovery::extension(entry.path())) {
            continue;
        }
        let relative = entry.path().strip_prefix(&root)?;
        let Some(path) = relative.to_str() else {
            skipped += 1;
            if errors.len() < 128 {
                errors.push(
                    json!({"path":relative.to_string_lossy(),"message":"Nome file non Unicode"}),
                );
            }
            continue;
        };
        let path = path.replace('\\', "/");
        let name = entry.file_name().to_str().unwrap_or_default();
        let candidate = if match_relative_path {
            path.as_str()
        } else {
            name
        };
        if !names.is_match(candidate) || !paths.is_match(&path) {
            continue;
        }
        if results.len() == options.limit {
            truncated = true;
            break;
        }
        results.push(json!({"path":path,"name":name,"kind":"file","line":1,"column":0}));
    }
    results.sort_by(|a, b| a["path"].as_str().cmp(&b["path"].as_str()));
    Ok(
        json!({"event":"file_results","results":results,"truncated":truncated,"timed_out":timed_out,
              "canceled":was_canceled,"scanned":scanned,"skipped":skipped,"errors":errors,
              "elapsed_ms":start.elapsed().as_millis()}),
    )
}

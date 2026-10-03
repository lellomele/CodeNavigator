use anyhow::{Result, bail};
use regex::{Regex, RegexBuilder};
use rusqlite::Connection;
use serde_json::{Value, json};
use std::time::{Duration, Instant};

pub fn matcher(pattern: &str, mode: &str, case_sensitive: bool) -> Result<Regex> {
    anyhow::ensure!(
        pattern.len() <= 4096,
        "Espressione troppo lunga (massimo 4096 byte)"
    );
    let expression = match mode {
        "literal" => regex::escape(pattern),
        "glob" => {
            let mut s = String::from("^");
            for ch in pattern.chars() {
                match ch {
                    '*' => s.push_str(".*"),
                    '?' => s.push('.'),
                    _ => s.push_str(&regex::escape(&ch.to_string())),
                }
            }
            s.push('$');
            s
        }
        "regex" => pattern.to_string(),
        _ => bail!("Modalità sconosciuta: {mode}; usare literal, glob o regex"),
    };
    RegexBuilder::new(&expression)
        .case_insensitive(!case_sensitive)
        .size_limit(2 * 1024 * 1024)
        .dfa_size_limit(2 * 1024 * 1024)
        .build()
        .map_err(|e| anyhow::anyhow!("Espressione non valida: {e}"))
}

#[derive(Clone, Copy)]
pub struct MatchOptions {
    pub case_sensitive: bool,
    pub whole_words: bool,
}

pub struct PathScope<'a> {
    pub pattern: &'a str,
    pub directory: &'a str,
}
pub fn directory_prefix(directory: &str) -> Result<String> {
    let normalized = directory.replace('\\', "/");
    let directory = normalized.trim_end_matches('/');
    anyhow::ensure!(
        !directory.starts_with('/')
            && !directory.contains(':')
            && !directory.split('/').any(|part| part == ".." || part == "."),
        "Cartella di ricerca non valida"
    );
    Ok(directory.to_string())
}

pub fn query(
    c: &Connection,
    pattern: &str,
    mode: &str,
    scope: PathScope<'_>,
    kind: &str,
    options: MatchOptions,
    limit: usize,
) -> Result<Value> {
    let MatchOptions {
        case_sensitive,
        whole_words,
    } = options;
    let filter = word_matcher(
        matcher(pattern, mode, case_sensitive)?,
        case_sensitive,
        whole_words,
    )?;
    let path = scope.pattern;
    let directory = directory_prefix(scope.directory)?;
    let paths = matcher(if path.is_empty() { "*" } else { path }, "glob", false)?;
    let start = Instant::now();
    c.progress_handler(
        10_000,
        Some(move || start.elapsed() > Duration::from_secs(2)),
    );
    struct ResetProgress<'a>(&'a Connection);
    impl Drop for ResetProgress<'_> {
        fn drop(&mut self) {
            self.0.progress_handler(0, None::<fn() -> bool>);
        }
    }
    let _reset = ResetProgress(c);
    // Trigrams narrow ASCII searches. Non-ASCII symbols remain candidates because
    // SQLite and the regex engine have different Unicode case-folding rules.
    let term = match mode {
        "literal" => pattern,
        "glob" => pattern
            .split(['*', '?'])
            .max_by_key(|p| p.len())
            .unwrap_or(""),
        _ => "",
    };
    let use_index = term.is_ascii() && term.len() >= 3;
    let condition = if use_index {
        " AND s.id IN (SELECT rowid FROM symbol_search WHERE symbol_search MATCH ?2 UNION SELECT id FROM symbols WHERE ascii_only=0)"
    } else {
        " AND ?2=''"
    };
    let sql = format!(
        "SELECT s.path,s.name,s.scope,s.kind,s.line,s.col,s.detail,f.status FROM symbols s JOIN files f ON s.path=f.path WHERE (?1='' OR s.kind=?1) AND (?3='' OR substr(s.path,1,length(?3)+1)=?3||'/'){condition} ORDER BY s.name COLLATE NOCASE,s.path,s.line"
    );
    let mut statement = c.prepare(&sql)?;
    let fts = if use_index {
        format!("\"{}\"", term.replace('"', "\"\""))
    } else {
        String::new()
    };
    let mut rows = statement.query(rusqlite::params![kind, fts, directory])?;
    let mut found = Vec::new();
    let mut scanned = 0;
    let mut truncated = false;
    let mut timed_out = false;
    loop {
        let r = match rows.next() {
            Ok(Some(r)) => r,
            Ok(None) => break,
            Err(e) => {
                if start.elapsed() >= Duration::from_secs(2) {
                    truncated = true;
                    timed_out = true;
                    break;
                } else {
                    return Err(e.into());
                }
            }
        };
        scanned += 1;
        if scanned % 256 == 0 && start.elapsed() > Duration::from_secs(2) {
            truncated = true;
            timed_out = true;
            break;
        }
        let name: String = r.get(1)?;
        let scope: String = r.get(2)?;
        let file: String = r.get(0)?;
        let qualified = if scope.is_empty() {
            name.clone()
        } else {
            format!("{scope}::{name}")
        };
        if !(filter.is_match(&name) || filter.is_match(&qualified)) || !paths.is_match(&file) {
            continue;
        }
        if found.len() == limit {
            truncated = true;
            break;
        }
        found.push(json!({"path":file,"name":name,"qualified":qualified,"scope":scope,"kind":r.get::<_,String>(3)?,"line":r.get::<_,i64>(4)?,"column":r.get::<_,i64>(5)?,"detail":r.get::<_,String>(6)?,"status":r.get::<_,String>(7)?}));
    }
    Ok(
        json!({"event":"results","results":found,"truncated":truncated,"timed_out":timed_out,"scanned":scanned,"indexed_filter":use_index,"elapsed_ms":start.elapsed().as_millis()}),
    )
}

// Text wildcards match a substring of a line; path and symbol globs remain anchored.
pub fn text_matcher(
    pattern: &str,
    mode: &str,
    sensitive: bool,
    whole_words: bool,
) -> Result<Regex> {
    if mode != "glob" {
        return word_matcher(matcher(pattern, mode, sensitive)?, sensitive, whole_words);
    }
    anyhow::ensure!(pattern.len() <= 4096, "Espressione troppo lunga");
    let mut expression = String::new();
    for ch in pattern.chars() {
        match ch {
            '*' => expression.push_str(".*?"),
            '?' => expression.push('.'),
            _ => expression.push_str(&regex::escape(&ch.to_string())),
        }
    }
    word_matcher(
        matcher(&expression, "regex", sensitive)?,
        sensitive,
        whole_words,
    )
}

fn word_matcher(regex: Regex, sensitive: bool, whole_words: bool) -> Result<Regex> {
    if whole_words {
        matcher(&format!(r"\b(?:{})\b", regex.as_str()), "regex", sensitive)
    } else {
        Ok(regex)
    }
}

pub fn source_text(
    c: &Connection,
    pattern: &str,
    mode: &str,
    scope: PathScope<'_>,
    options: MatchOptions,
    limit: usize,
) -> Result<Value> {
    let MatchOptions {
        case_sensitive,
        whole_words,
    } = options;
    use std::{fs, io::Read, path::PathBuf};
    let matcher = text_matcher(pattern, mode, case_sensitive, whole_words)?;
    let path = scope.pattern;
    let directory = directory_prefix(scope.directory)?;
    let paths = crate::search::matcher(if path.is_empty() { "*" } else { path }, "glob", false)?;
    let root: String = c.query_row("SELECT value FROM meta WHERE key='root'", [], |r| r.get(0))?;
    let root = fs::canonicalize(PathBuf::from(root))?;
    let mut q = c.prepare(
        "SELECT path FROM files WHERE (?1='' OR substr(path,1,length(?1)+1)=?1||'/') ORDER BY path",
    )?;
    let mut rows = q.query([directory])?;
    let mut found = Vec::new();
    let mut errors = Vec::new();
    let mut skipped = 0;
    let mut scanned = 0;
    let start = Instant::now();
    let mut truncated = false;
    if !pattern.is_empty() {
        'files: while let Some(row) = rows.next()? {
            if start.elapsed() > Duration::from_secs(2) {
                truncated = true;
                break;
            }
            let file: String = row.get(0)?;
            if !paths.is_match(&file) {
                continue;
            }
            let data = (|| -> Result<String> {
                let source = fs::canonicalize(root.join(&file))?;
                anyhow::ensure!(
                    source.starts_with(&root),
                    "File fuori dalla cartella del progetto"
                );
                let mut bytes = Vec::new();
                fs::File::open(source)?
                    .take(32 * 1024 * 1024 + 1)
                    .read_to_end(&mut bytes)?;
                anyhow::ensure!(bytes.len() <= 32 * 1024 * 1024, "File troppo grande");
                Ok(crate::textfile::Text::decode(&bytes)?.text)
            })();
            let data = match data {
                Ok(data) => data,
                Err(e) => {
                    skipped += 1;
                    if errors.len() < 100 {
                        errors.push(json!({"path":file,"error":e.to_string()}));
                    }
                    continue;
                }
            };
            scanned += 1;
            for (line, text) in data
                .strip_prefix('\u{feff}')
                .unwrap_or(&data)
                .lines()
                .enumerate()
            {
                if line % 256 == 0 && start.elapsed() > Duration::from_secs(2) {
                    truncated = true;
                    break 'files;
                }
                for hit in matcher.find_iter(text) {
                    if found.len() == limit {
                        truncated = true;
                        break 'files;
                    }
                    let excerpt: String = text.chars().take(500).collect();
                    found.push(json!({"name":excerpt,"qualified":excerpt,"path":file,"line":line+1,"column":hit.start(),"length":hit.len(),"kind":"text","scope":"","detail":excerpt,"status":"ready"}));
                }
            }
        }
    }
    Ok(
        json!({"event":"results","results":found,"errors":errors,"skipped":skipped,"truncated":truncated,"scanned":scanned,"elapsed_ms":start.elapsed().as_millis()}),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn modes_are_distinct() {
        assert!(matcher("a.b", "literal", true).unwrap().is_match("a.b"));
        assert!(!matcher("a.b", "literal", true).unwrap().is_match("axb"));
        assert!(
            matcher("get*?", "glob", false)
                .unwrap()
                .is_match("GetValue")
        );
        assert!(!matcher("get*", "glob", true).unwrap().is_match("forget"));
        assert!(
            matcher(r"^(get|set)[A-Z]", "regex", true)
                .unwrap()
                .is_match("getValue")
        );
    }
    #[test]
    fn invalid_and_unbounded_patterns_fail() {
        assert!(matcher("[", "regex", true).is_err());
        assert!(matcher(&"x".repeat(4097), "regex", true).is_err());
        assert!(matcher(r"(a)\1", "regex", true).is_err());
    }
}

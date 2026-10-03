//! Conservative syntactic references. Names are not resolved by a compiler/type checker.
use anyhow::Result;
use regex::Regex;
use rusqlite::{Connection, params};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::{
    collections::{HashMap, HashSet},
    sync::LazyLock,
};
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Edge {
    pub source: String,
    pub target: String,
    pub kind: String,
    pub line: usize,
    pub column: usize,
}
struct Token<'a> {
    text: &'a str,
    line: usize,
    col: usize,
    string: bool,
}
static LEX: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r#"(?s)/\*.*?\*/|//[^\n]*|""".*?"""|'''.*?'''|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|`(?:\\.|[^`\\])*`|[\p{L}_$][\p{L}\p{N}_$]*|\+=|-=|\*=|/=|\+\+|--|==|!=|<=|>=|=>|::|[^\s]"#).unwrap()
});
fn ident(s: &str) -> bool {
    s.chars()
        .next()
        .is_some_and(|c| c.is_alphabetic() || c == '_' || c == '$')
}
fn keywords(s: &str) -> bool {
    matches!(
        s,
        "if" | "else"
            | "for"
            | "while"
            | "switch"
            | "catch"
            | "return"
            | "sizeof"
            | "new"
            | "delete"
            | "class"
            | "struct"
            | "interface"
            | "enum"
            | "namespace"
            | "fn"
            | "def"
            | "function"
            | "let"
            | "const"
            | "var"
            | "mut"
            | "public"
            | "private"
            | "protected"
            | "static"
            | "void"
            | "int"
            | "char"
            | "float"
            | "double"
            | "bool"
            | "true"
            | "false"
            | "null"
            | "None"
            | "self"
            | "this"
            | "import"
            | "from"
            | "include"
            | "require"
            | "extends"
            | "implements"
            | "as"
            | "use"
            | "package"
            | "mod"
            | "pub"
            | "impl"
            | "trait"
            | "in"
            | "and"
            | "or"
            | "not"
            | "with"
            | "async"
            | "await"
            | "try"
            | "except"
            | "raise"
            | "pass"
            | "break"
            | "continue"
            | "throw"
            | "throws"
            | "virtual"
            | "override"
            | "final"
            | "export"
            | "default"
            | "type"
            | "typedef"
            | "unsigned"
            | "long"
            | "short"
            | "auto"
            | "volatile"
            | "extern"
    )
}
pub fn analyze(text: &str, extension: &str) -> Result<Vec<Edge>> {
    if !matches!(
        extension,
        "c" | "h"
            | "cpp"
            | "hpp"
            | "cc"
            | "cxx"
            | "hxx"
            | "java"
            | "cs"
            | "js"
            | "jsx"
            | "ts"
            | "tsx"
            | "py"
            | "pyw"
            | "rs"
            | "php"
    ) {
        return Ok(Vec::new());
    }
    let started = std::time::Instant::now();
    let indents: Vec<usize> = text
        .lines()
        .map(|line| line.chars().take_while(|c| c.is_whitespace()).count())
        .collect();
    let python = matches!(extension, "py" | "pyw");
    let mut tokens = Vec::new();
    let mut line = 1;
    let mut line_start = 0;
    let mut previous = 0;
    let mut hash_comment = 0;
    for m in LEX.find_iter(text) {
        for (i, b) in text.as_bytes()[previous..m.start()].iter().enumerate() {
            if *b == b'\n' {
                line += 1;
                line_start = previous + i + 1;
            }
        }
        let s = m.as_str();
        let start_line = line;
        let col = m.start() - line_start;
        for (i, b) in s.bytes().enumerate() {
            if b == b'\n' {
                line += 1;
                line_start = m.start() + i + 1;
            }
        }
        previous = m.end();
        if python && s == "#" {
            hash_comment = start_line;
        }
        if start_line == hash_comment || (!python && (s.starts_with("//") || s.starts_with("/*"))) {
            continue;
        }
        if tokens.len() % 1024 == 0 {
            anyhow::ensure!(started.elapsed().as_secs() < 10, "Cross-reference timeout");
        }
        tokens.push(Token {
            text: s,
            line: start_line,
            col,
            string: s.starts_with(['\'', '"', '`']),
        });
        anyhow::ensure!(
            tokens.len() <= 500_000,
            "Cross-reference token limit exceeded"
        );
    }
    let mut pairs = HashMap::new();
    let mut stack: Vec<usize> = Vec::new();
    for (i, t) in tokens.iter().enumerate() {
        if matches!(t.text, "(" | "{" | "[") {
            stack.push(i);
        }
        if matches!(t.text, ")" | "}" | "]")
            && let Some(start) = stack.pop()
            && matches!(
                (tokens[start].text, t.text),
                ("(", ")") | ("{", "}") | ("[", "]")
            )
        {
            pairs.insert(start, i);
        }
    }
    let at = |i: usize| tokens.get(i).map(|t| t.text).unwrap_or("");
    let mut edges = Vec::new();
    let mut definitions = HashSet::new();
    let mut scopes: Vec<(usize, usize, String)> = Vec::new();
    // Definitions and their lexical bodies, without guessing across unmatched braces.
    for i in 0..tokens.len() {
        let t = &tokens[i];
        if !ident(t.text)
            || (keywords(t.text) && !(i > 0 && matches!(at(i - 1), "fn" | "def" | "function")))
            || t.string
        {
            continue;
        }
        let prev = if i > 0 { at(i - 1) } else { "" };
        let class = matches!(prev, "class" | "struct" | "interface" | "trait" | "enum");
        let explicit = matches!(prev, "fn" | "def" | "function");
        let close = if at(i + 1) == "(" {
            pairs.get(&(i + 1)).copied()
        } else {
            None
        };
        let mut body = close.map(|v| v + 1).unwrap_or(i + 1);
        if class {
            while body < tokens.len()
                && tokens[body].line <= t.line + 8
                && !matches!(at(body), "{" | ":" | ";")
            {
                body += 1;
            }
        }
        if !class && close.is_some() {
            while matches!(at(body), "const" | "noexcept" | "override" | "final" | "&") {
                body += 1;
            }
        }
        let function = explicit
            || (close.is_some()
                && at(body) == "{"
                && !matches!(prev, "." | "->" | "new" | "return"));
        if !class && !function {
            continue;
        }
        if explicit && !python {
            while body < tokens.len() && body < i + 512 && !matches!(at(body), "{" | ";") {
                body += 1;
            }
        }
        definitions.insert(i);
        edges.push(Edge {
            source: String::new(),
            target: t.text.into(),
            kind: "declaration".into(),
            line: t.line,
            column: t.col,
        });
        if python && (explicit || class) {
            let indent = indents[t.line - 1];
            let end = tokens
                .iter()
                .enumerate()
                .skip(i + 1)
                .find(|(_, n)| n.line > t.line && n.col <= indent)
                .map(|(j, _)| j)
                .unwrap_or(tokens.len());
            scopes.push((i, end, t.text.into()));
        } else {
            if at(body) == ":" && class {
                while body < tokens.len() && !matches!(at(body), "{" | ";") {
                    body += 1;
                }
            }
            if let Some(end) = pairs.get(&body) {
                scopes.push((i, *end, t.text.into()));
            }
        }
        if class {
            let mut bases = false;
            for (j, n) in tokens.iter().enumerate().take(body).skip(i + 1) {
                if matches!(n.text, "extends" | "implements" | ":" | "(") {
                    bases = true;
                    continue;
                }
                if bases && ident(n.text) && !keywords(n.text) {
                    definitions.insert(j);
                    edges.push(Edge {
                        source: t.text.into(),
                        target: n.text.into(),
                        kind: "inherits".into(),
                        line: n.line,
                        column: n.col,
                    });
                }
            }
            // Python bases precede ':'; the generic body scan already includes them.
        }
    }
    let mut owners = vec![None; tokens.len()];
    let mut active: Vec<usize> = Vec::new();
    let mut next_scope = 0;
    for (i, owner) in owners.iter_mut().enumerate() {
        while active.last().is_some_and(|j| scopes[*j].1 <= i) {
            active.pop();
        }
        *owner = active.last().copied();
        if next_scope < scopes.len() && scopes[next_scope].0 == i {
            anyhow::ensure!(
                active.len() < 1024,
                "Cross-reference nesting limit exceeded"
            );
            active.push(next_scope);
            next_scope += 1;
        }
    }
    let scope = |i: usize| owners[i].map(|j| scopes[j].2.clone()).unwrap_or_default();
    let mut dependency_tokens = HashSet::new();
    for i in 0..tokens.len() {
        if !matches!(at(i), "include" | "import" | "from" | "require" | "use") {
            continue;
        }
        let mut target = String::new();
        for (j, n) in tokens.iter().enumerate().skip(i + 1).take(100) {
            if n.line != tokens[i].line || n.text == ";" {
                break;
            }
            dependency_tokens.insert(j);
            if n.string {
                target = n.text.trim_matches(['\'', '"', '`']).into();
                break;
            }
            if n.text == "import" || n.text == "as" {
                break;
            }
            if n.text == ">" {
                break;
            }
            if ident(n.text) || matches!(n.text, "." | "/" | "::" | "-") {
                target.push_str(n.text);
            }
        }
        if !target.is_empty() {
            edges.push(Edge {
                source: String::new(),
                target,
                kind: "depends".into(),
                line: tokens[i].line,
                column: tokens[i].col,
            });
        }
    }
    for (i, t) in tokens.iter().enumerate() {
        if t.string
            || definitions.contains(&i)
            || dependency_tokens.contains(&i)
            || !ident(t.text)
            || (keywords(t.text) && !(extension == "rs" && t.text == "new" && at(i + 1) == "("))
        {
            continue;
        }
        let prev = if i > 0 { at(i - 1) } else { "" };
        let next = at(i + 1);
        let declaration = matches!(
            prev,
            "let"
                | "var"
                | "const"
                | "mut"
                | "int"
                | "char"
                | "float"
                | "double"
                | "bool"
                | "auto"
                | "long"
                | "short"
        ) || (python && next == "=" && scope(i).is_empty())
            || (extension == "rs"
                && next == ":"
                && owners[i].is_some_and(|j| {
                    let start = scopes[j].0;
                    start > 0 && matches!(at(start - 1), "struct" | "enum")
                }));
        let kind = if next == "(" {
            "calls"
        } else if declaration {
            "declaration"
        } else if matches!(next, "=" | "+=" | "-=" | "*=" | "/=" | "++" | "--")
            || matches!(prev, "++" | "--")
        {
            "write"
        } else {
            "read"
        };
        edges.push(Edge {
            source: scope(i),
            target: t.text.into(),
            kind: kind.into(),
            line: t.line,
            column: t.col,
        });
        if declaration && next == "=" {
            edges.push(Edge {
                source: scope(i),
                target: t.text.into(),
                kind: "write".into(),
                line: t.line,
                column: t.col,
            });
        }
        if matches!(next, "+=" | "-=" | "*=" | "/=" | "++" | "--") || matches!(prev, "++" | "--") {
            edges.push(Edge {
                source: scope(i),
                target: t.text.into(),
                kind: "read".into(),
                line: t.line,
                column: t.col,
            });
        }
    }
    Ok(edges)
}
pub fn query(
    c: &Connection,
    subject: &str,
    relation: &str,
    direction: &str,
    scope: crate::search::PathScope<'_>,
) -> Result<Value> {
    anyhow::ensure!(subject.len() <= 4096, "Subject too long");
    anyhow::ensure!(
        matches!(
            relation,
            "all" | "calls" | "read" | "write" | "declaration" | "inherits" | "depends"
        ),
        "Invalid relation"
    );
    anyhow::ensure!(
        matches!(direction, "incoming" | "outgoing"),
        "Invalid direction"
    );
    let path = scope.pattern;
    let directory = crate::search::directory_prefix(scope.directory)?;
    let paths = crate::search::matcher(if path.is_empty() { "*" } else { path }, "glob", false)?;
    let column = if direction == "outgoing" {
        "source"
    } else {
        "target"
    };
    let mut q=c.prepare(&format!("SELECT x.path,x.source,x.target,x.kind,x.line,x.col,f.status FROM xrefs x JOIN files f ON f.path=x.path WHERE (?1='' OR x.{column}=?1 OR (x.kind='depends' AND ?2='outgoing' AND x.path=?1)) AND (?3='all' OR x.kind=?3) AND (?4='' OR substr(x.path,1,length(?4)+1)=?4||'/') ORDER BY x.path,x.line"))?;
    let mut rows = q.query(params![subject, direction, relation, directory])?;
    let mut results = Vec::new();
    let start = std::time::Instant::now();
    let mut truncated = false;
    while let Some(r) = rows.next()? {
        if results.len() == 10000 || start.elapsed().as_secs() >= 3 {
            truncated = true;
            break;
        }
        let file: String = r.get(0)?;
        if !paths.is_match(&file) {
            continue;
        }
        results.push(json!({"path":file,"source":r.get::<_,String>(1)?,"target":r.get::<_,String>(2)?,"kind":r.get::<_,String>(3)?,"line":r.get::<_,i64>(4)?,"column":r.get::<_,i64>(5)?,"status":r.get::<_,String>(6)?,"confidence":"syntactic"}));
    }

    Ok(
        json!({"event":"xrefs","results":results,"truncated":truncated,"analysis":"syntactic","generation":c.query_row("SELECT value FROM meta WHERE key='generation'",[],|r|r.get::<_,String>(0)).unwrap_or_default()}),
    )
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn calls_scopes_comments_and_access() {
        let e = analyze(
            "void a(){ int y=0; y += 1; b(y); /* c(); */ } void d(){ b(); }",
            "cpp",
        )
        .unwrap();
        assert!(
            e.iter()
                .any(|e| e.source == "a" && e.target == "b" && e.kind == "calls")
        );
        assert!(
            e.iter()
                .any(|e| e.source == "d" && e.target == "b" && e.kind == "calls")
        );
        assert!(!e.iter().any(|e| e.target == "c"));
        assert!(e.iter().any(|e| e.target == "y" && e.kind == "write"));
    }
    #[test]
    fn inheritance_dependencies_and_strings() {
        let e=analyze("#include \"base.h\"\nclass Child : public Base { void go(){ f(); } };\nconst char* s=\"fake()\";", "cpp").unwrap();
        assert!(
            e.iter()
                .any(|e| e.kind == "depends" && e.target == "base.h")
        );
        assert!(e.iter().any(|e| e.kind == "inherits" && e.target == "Base"));
        assert!(!e.iter().any(|e| e.target == "fake"));
    }
    #[test]
    fn rust_fields_and_new_method() {
        let e=analyze("struct Catalog { alpha: i32 }\nimpl Catalog { fn new() -> Self { Self { alpha: value() } } }","rs").unwrap();
        assert!(
            e.iter()
                .any(|e| e.target == "alpha" && e.kind == "declaration")
        );
        assert!(
            e.iter()
                .any(|e| e.source == "new" && e.target == "value" && e.kind == "calls")
        );
    }
    #[test]
    fn python_scope_and_comments() {
        let e = analyze("def a():\n    b()\n    # fake()\ndef c():\n    d()\n", "py").unwrap();
        assert!(e.iter().any(|e| e.source == "a" && e.target == "b"));
        assert!(e.iter().any(|e| e.source == "c" && e.target == "d"));
        assert!(!e.iter().any(|e| e.target == "fake"));
    }
}

/// Parse one snapshot in a bounded child process; a crash cannot corrupt the parent index.
pub fn run(
    text: &str,
    extension: &str,
    work: &std::path::Path,
    timeout_ms: u64,
    flag: &std::sync::atomic::AtomicBool,
    cancel: &Option<std::path::PathBuf>,
) -> Result<Vec<Edge>> {
    use std::{
        fs,
        process::{Command, Stdio},
        time::{Duration, Instant},
    };
    if !matches!(
        extension,
        "c" | "h"
            | "cpp"
            | "hpp"
            | "cc"
            | "cxx"
            | "hxx"
            | "java"
            | "cs"
            | "js"
            | "jsx"
            | "ts"
            | "tsx"
            | "py"
            | "pyw"
            | "rs"
            | "php"
    ) {
        return Ok(Vec::new());
    }
    fs::create_dir_all(work)?;
    let input = work.join("xref-input.txt");
    let output = work.join("xref-output.json");
    let error = work.join("xref-error.txt");
    fs::write(&input, text)?;
    let mut command = Command::new(std::env::current_exe()?);
    command
        .args(["xref-worker", "--input"])
        .arg(&input)
        .args(["--extension", extension])
        .stdin(Stdio::null())
        .stdout(fs::File::create(&output)?)
        .stderr(fs::File::create(&error)?);
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        command.creation_flags(0x08000000);
    }
    let mut child = command.spawn()?;
    #[cfg(windows)]
    let job = match crate::parser::Job::assign(&child) {
        Ok(job) => job,
        Err(e) => {
            let _ = child.kill();
            let _ = child.wait();
            return Err(e);
        }
    };
    let start = Instant::now();
    let result = (|| -> Result<Vec<Edge>> {
        loop {
            anyhow::ensure!(
                !crate::parser::canceled(flag, cancel),
                "Cross-reference analysis canceled"
            );
            anyhow::ensure!(
                start.elapsed() < Duration::from_millis(timeout_ms),
                "Cross-reference parser timeout"
            );
            anyhow::ensure!(
                fs::metadata(&output)?.len() <= 32 * 1024 * 1024
                    && fs::metadata(&error)?.len() <= 1024 * 1024,
                "Cross-reference output limit exceeded"
            );
            if let Some(status) = child.try_wait()? {
                anyhow::ensure!(
                    status.success(),
                    "Cross-reference parser failed: {}",
                    String::from_utf8_lossy(&fs::read(&error)?)
                );
                break;
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        let bytes = fs::read(&output)?;
        anyhow::ensure!(
            bytes.len() <= 32 * 1024 * 1024,
            "Cross-reference output limit exceeded"
        );
        Ok(serde_json::from_slice(&bytes)?)
    })();
    #[cfg(windows)]
    drop(job);
    if result.is_err() {
        let _ = child.kill();
        let _ = child.wait();
    }
    result
}

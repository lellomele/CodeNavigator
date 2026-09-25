use anyhow::Result;
use serde_json::{Value, json};
use std::{collections::BTreeMap, fs, io::Read, path::Path};
use walkdir::{DirEntry, WalkDir};

pub const LIMIT: u64 = 32 * 1024 * 1024;
const KNOWN: &str = "c h cpp hpp cc hh cxx hxx c++ inl inc cs java kt kts scala rs go py pyw pyi js jsx mjs cjs ts tsx tcl tk itcl itk test php php3 php4 phtml rb erb pl pm r lua sh bash zsh ps1 psm1 bat cmd swift m mm dart ex exs erl hrl hs lhs ml mli clj cljs cljc lisp cl el pas pp d f for f90 f95 f03 f08 cob cbl asm s sql html htm css scss sass less vue svelte xml xsl xsd json jsonc yaml yml toml ini cfg conf properties cmake make mk m4 ac src exl proto graphql groovy gradle v sv vhd vhdl tex md rst txt";
pub fn extension(p: &Path) -> String {
    p.extension()
        .and_then(|s| s.to_str())
        .map(|s| s.to_lowercase())
        .unwrap_or_else(|| {
            format!(
                "@{}",
                p.file_name()
                    .unwrap_or_default()
                    .to_string_lossy()
                    .to_lowercase()
            )
        })
}
pub fn classic(ext: &str) -> bool {
    KNOWN.split_whitespace().any(|s| s == ext)
        || [
            "@makefile",
            "@dockerfile",
            "@cmakelists.txt",
            "@rakefile",
            "@gemfile",
        ]
        .contains(&ext)
}
pub fn include(e: &DirEntry) -> bool {
    e.depth() == 0
        || ![
            ".git",
            ".svn",
            ".sn-index",
            "node_modules",
            "target",
            "build",
            "dist",
            ".venv",
            "__pycache__",
        ]
        .contains(&e.file_name().to_string_lossy().as_ref())
}
pub fn read_text(p: &Path) -> Result<Vec<u8>> {
    let mut b = Vec::new();
    fs::File::open(p)?.take(LIMIT + 1).read_to_end(&mut b)?;
    anyhow::ensure!(b.len() as u64 <= LIMIT, "File oltre 32 MiB");
    crate::textfile::Text::decode(&b)?;
    Ok(b)
}
pub fn scan(root: &Path) -> Result<Value> {
    let root = fs::canonicalize(root)?;
    let mut counts = BTreeMap::<String, (usize, u64)>::new();
    let mut skipped = 0;
    let mut errors = Vec::new();
    for e in WalkDir::new(root)
        .follow_links(false)
        .into_iter()
        .filter_entry(include)
    {
        let e = match e {
            Ok(e) => e,
            Err(e) => {
                errors.push(e.to_string());
                continue;
            }
        };
        if !e.file_type().is_file() {
            continue;
        }
        match read_text(e.path()) {
            Ok(b) => {
                let count = counts.entry(extension(e.path())).or_default();
                count.0 += 1;
                count.1 += b.len() as u64;
            }
            Err(_) => skipped += 1,
        }
    }
    let extensions: Vec<Value> = counts.into_iter().map(|(ext, (count, bytes))| json!({"selected":classic(&ext),"extension":ext,"files":count,"bytes":bytes})).collect();
    Ok(json!({"event":"discovery","extensions":extensions,"skipped":skipped,"errors":errors}))
}

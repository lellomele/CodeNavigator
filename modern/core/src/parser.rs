use anyhow::{Context, Result, bail};
use serde::{Deserialize, Serialize};
use std::{
    fs::{self, File},
    path::{Path, PathBuf},
    process::{Command, Stdio},
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    },
    time::{Duration, Instant},
};

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Symbol {
    pub name: String,
    pub scope: String,
    pub kind: String,
    pub line: u32,
    pub column: u32,
    pub detail: String,
}
pub fn executable(path: &Path) -> Option<&'static str> {
    if path.file_name().and_then(|n| n.to_str()) == Some("configure.in") {
        return Some("m4browser");
    }
    Some(
        match path.extension()?.to_str()?.to_ascii_lowercase().as_str() {
            "c" | "h" | "cpp" | "hpp" | "cc" | "hh" | "cxx" | "hxx" | "c++" => "cbrowser",
            "py" | "pyw" => "pybrowser",
            "tcl" | "tk" | "itcl" | "itk" | "test" => "tbrowser",
            "java" => "jbrowser",
            "f" | "for" => "fbrowser",
            "cbl" | "cob" => "obrowser",
            "php" | "php3" | "php4" => "phpbrowser",
            "m4" | "ac" => "m4browser",
            "src" => "ibrowser",
            "asm" | "s" => "abrowser",
            "exl" => "exlbrowser",
            _ => return None,
        },
    )
}
pub fn binary(dir: &Path, name: &str) -> PathBuf {
    dir.join(format!("{name}{}", std::env::consts::EXE_SUFFIX))
}
pub fn decode(output: &[u8]) -> Result<Vec<Symbol>> {
    let text = std::str::from_utf8(output).context("Output parser non UTF-8")?;
    let mut result = Vec::new();
    let mut recognized = false;
    for row in text.lines() {
        if row.starts_with("Status:") || row.is_empty() {
            continue;
        }
        let Some((id, rest)) = row.split_once(';') else {
            continue;
        };
        let Ok(id) = id.parse::<u32>() else { continue };
        anyhow::ensure!(id <= 30, "Tipo di record PAF sconosciuto: {id}");
        if id == 0 {
            let (file, language) = rest
                .split_once(';')
                .context("Intestazione PAF incompleta")?;
            anyhow::ensure!(
                !file.is_empty() && !language.is_empty(),
                "Intestazione PAF non valida"
            );
            recognized = true;
        }
        let kind = match id {
            1 => "type",
            2 => "class",
            3 => "method",
            4 => "field",
            5 => "enum",
            6 => "constant",
            7 => "macro",
            8 | 9 => "function",
            10 => "variable",
            11 => "common",
            12 => "field",
            17 => "method-declaration",
            18 => "declaration",
            19 => "enum-value",
            20 => "union",
            21 => "friend",
            22 => "namespace",
            23 => "exception",
            24 => "local",
            25 => "variable-declaration",
            26 => "include",
            _ => continue,
        };
        let (key, detail) = rest.split_once(';').context("Record PAF incompleto")?;
        let pieces: Vec<_> = key.split('\u{1}').collect();
        let pos = pieces.iter().position(|s| {
            let Some((a, b)) = s.split_once('.') else {
                return false;
            };
            !a.is_empty()
                && !b.is_empty()
                && a.bytes().all(|b| b.is_ascii_digit())
                && b.bytes().all(|b| b.is_ascii_digit())
        });
        let Some(pos) = pos else { continue };
        if pos == 0 {
            continue;
        }
        let (line, col) = pieces[pos].split_once('.').unwrap();
        let name = pieces[pos - 1].to_string();
        let scope = pieces[..pos - 1].join("::");
        result.push(Symbol {
            name,
            scope,
            kind: kind.into(),
            line: line.parse()?,
            column: col.parse()?,
            detail: detail.replace('\u{1}', " | "),
        });
    }
    anyhow::ensure!(recognized, "Nessun record PAF riconoscibile dal parser");
    Ok(result)
}

pub fn canceled(flag: &AtomicBool, cancel: &Option<PathBuf>) -> bool {
    flag.load(Ordering::Relaxed) || cancel.as_ref().is_some_and(|p| p.exists())
}

// A job owns all descendants: closing the job also handles timeout and unwinding.
#[cfg(windows)]
pub(crate) struct Job(windows_sys::Win32::Foundation::HANDLE);
#[cfg(windows)]
impl Job {
    pub(crate) fn assign(child: &std::process::Child) -> Result<Self> {
        use std::os::windows::io::AsRawHandle;
        use windows_sys::Win32::System::JobObjects::*;
        unsafe {
            let handle = CreateJobObjectW(std::ptr::null(), std::ptr::null());
            anyhow::ensure!(
                !handle.is_null(),
                "Creazione Job Object: {}",
                std::io::Error::last_os_error()
            );
            let job = Self(handle);
            let mut info: JOBOBJECT_EXTENDED_LIMIT_INFORMATION = std::mem::zeroed();
            info.BasicLimitInformation.LimitFlags =
                JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
            info.ProcessMemoryLimit = 512 * 1024 * 1024;
            anyhow::ensure!(
                SetInformationJobObject(
                    handle,
                    JobObjectExtendedLimitInformation,
                    &info as *const _ as *const _,
                    std::mem::size_of_val(&info) as u32
                ) != 0,
                "Configurazione Job Object: {}",
                std::io::Error::last_os_error()
            );
            anyhow::ensure!(
                AssignProcessToJobObject(handle, child.as_raw_handle()) != 0,
                "Associazione Job Object: {}",
                std::io::Error::last_os_error()
            );
            Ok(job)
        }
    }
}
#[cfg(windows)]
impl Drop for Job {
    fn drop(&mut self) {
        unsafe {
            windows_sys::Win32::Foundation::CloseHandle(self.0);
        }
    }
}

pub struct Parsed {
    pub xrefs: Vec<crate::xref::Edge>,
    pub symbols: Vec<Symbol>,
    pub stderr: String,
    pub elapsed_ms: u128,
}
pub fn run(
    exe: &Path,
    bytes: &[u8],
    extension: &str,
    work: &Path,
    timeout_ms: u64,
    flag: Arc<AtomicBool>,
    cancel: &Option<PathBuf>,
) -> Result<Parsed> {
    fs::create_dir_all(work)?;
    let input = format!("input.{extension}");
    fs::write(work.join(&input), bytes)?;
    let out = work.join("stdout.paf");
    let err = work.join("stderr.txt");
    let mut cmd = Command::new(exe);
    cmd.arg(&input)
        .current_dir(work)
        .stdin(Stdio::null())
        .stdout(File::create(&out)?)
        .stderr(File::create(&err)?);
    if let Some(bin) = exe
        .parent()
        .and_then(Path::parent)
        .and_then(Path::parent)
        .map(|p| p.join("bin"))
    {
        let mut paths = vec![bin];
        if let Some(old) = std::env::var_os("PATH") {
            paths.extend(std::env::split_paths(&old))
        }
        cmd.env("PATH", std::env::join_paths(paths)?);
    }
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x08000000);
    }
    let start = Instant::now();
    let mut child = cmd
        .spawn()
        .with_context(|| format!("Avvio parser {}", exe.display()))?;
    #[cfg(windows)]
    let job = match Job::assign(&child) {
        Ok(j) => j,
        Err(e) => {
            let _ = child.kill();
            let _ = child.wait();
            return Err(e);
        }
    };
    let outcome = (|| -> Result<()> {
        loop {
            if canceled(&flag, cancel) {
                bail!("Indicizzazione annullata")
            }
            if start.elapsed() > Duration::from_millis(timeout_ms) {
                bail!("Timeout parser dopo {timeout_ms} ms")
            }
            if fs::metadata(&out)?.len() > 64 * 1024 * 1024
                || fs::metadata(&err)?.len() > 4 * 1024 * 1024
            {
                bail!("Output parser oltre il limite")
            }
            if let Some(status) = child.try_wait()? {
                anyhow::ensure!(status.success(), "Parser terminato con {status}");
                return Ok(());
            }
            std::thread::sleep(Duration::from_millis(20));
        }
    })();
    #[cfg(windows)]
    drop(job);
    if outcome.is_err() {
        let _ = child.kill();
        let _ = child.wait();
    }
    let diagnostic = String::from_utf8_lossy(&fs::read(&err).unwrap_or_default()).into_owned();
    outcome.with_context(|| {
        format!(
            "stderr: {}",
            diagnostic.chars().take(8192).collect::<String>()
        )
    })?;
    let output = fs::read(&out)?;
    anyhow::ensure!(
        output.len() <= 64 * 1024 * 1024,
        "Output parser oltre il limite"
    );
    Ok(Parsed {
        xrefs: Vec::new(),
        symbols: decode(&output)?,
        stderr: diagnostic,
        elapsed_ms: start.elapsed().as_millis(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn paf_scope_position_and_bad_input() {
        let s =
            decode(b"0;input.cpp;c++\x01\n3;Widget\x01run\x01000012.004\x01input.cpp;12.7\x01{}\n")
                .unwrap();
        assert_eq!(s[0].scope, "Widget");
        assert_eq!(s[0].name, "run");
        assert_eq!(s[0].line, 12);
        assert!(decode(b"garbage").is_err());
        assert!(decode(b"999;garbage").is_err());
        assert!(decode(b"0;broken").is_err());
    }
}

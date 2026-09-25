use anyhow::Result;
use serde_json::{Value, json};
use std::{
    fs::{File, OpenOptions},
    io::Write,
    path::Path,
    time::{SystemTime, UNIX_EPOCH},
};

pub fn now() -> u128 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis()
}
pub fn emit(value: &Value) {
    println!("{value}");
}
pub struct Log {
    file: File,
    run: String,
}
impl Log {
    pub fn new(dir: &Path, run: &str) -> Result<Self> {
        std::fs::create_dir_all(dir)?;
        let file = OpenOptions::new()
            .create_new(true)
            .write(true)
            .open(dir.join(format!("{run}.jsonl")))?;
        Ok(Self {
            file,
            run: run.into(),
        })
    }
    pub fn event(&mut self, name: &str, data: Value) -> Result<()> {
        let record = json!({"schema":1,"time_unix_ms":now(),"run_id":self.run,"pid":std::process::id(),"version":env!("CARGO_PKG_VERSION"),"event":name,"data":data});
        writeln!(self.file, "{record}")?;
        self.file.flush()?;
        Ok(())
    }
    pub fn sync(&self) -> Result<()> {
        self.file.sync_all()?;
        Ok(())
    }
}

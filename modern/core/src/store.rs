use anyhow::{Context, Result};
use rusqlite::{Connection, OpenFlags};
use serde_json::{Value, json};
use std::{path::Path, time::Duration};

pub fn read(path: &Path) -> Result<Connection> {
    let c = Connection::open_with_flags(path, OpenFlags::SQLITE_OPEN_READ_ONLY)
        .context("Aprire prima un progetto indicizzato")?;
    c.busy_timeout(Duration::from_secs(3))?;
    let version: i64 = c.query_row("PRAGMA user_version", [], |r| r.get(0))?;
    anyhow::ensure!(
        (2..=3).contains(&version),
        "Aggiornare l'indice: versione del database {version}"
    );
    Ok(c)
}
pub fn write(path: &Path) -> Result<Connection> {
    let c = Connection::open(path)?;
    c.busy_timeout(Duration::from_secs(5))?;
    c.pragma_update(None, "foreign_keys", true)?;
    c.pragma_update(None, "journal_mode", "WAL")?;
    c.pragma_update(None, "synchronous", "FULL")?;
    let version: i64 = c.query_row("PRAGMA user_version", [], |r| r.get(0))?;
    anyhow::ensure!(version <= 3, "Database creato da una versione più recente");
    c.execute_batch("BEGIN;
        CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS files(path TEXT PRIMARY KEY,hash TEXT NOT NULL,parser TEXT NOT NULL,status TEXT NOT NULL,error TEXT NOT NULL DEFAULT '');
        CREATE TABLE IF NOT EXISTS symbols(id INTEGER PRIMARY KEY,path TEXT NOT NULL REFERENCES files(path) ON DELETE CASCADE,name TEXT NOT NULL,scope TEXT NOT NULL,kind TEXT NOT NULL,line INTEGER NOT NULL,col INTEGER NOT NULL,detail TEXT NOT NULL);
        CREATE INDEX IF NOT EXISTS symbol_name ON symbols(name COLLATE NOCASE);
        CREATE INDEX IF NOT EXISTS symbol_path ON symbols(path);
        CREATE TABLE IF NOT EXISTS runs(id TEXT PRIMARY KEY,started INTEGER NOT NULL,finished INTEGER,status TEXT NOT NULL,summary TEXT NOT NULL DEFAULT '{}');
        CREATE TABLE IF NOT EXISTS pending_files(path TEXT PRIMARY KEY,hash TEXT NOT NULL,parser TEXT NOT NULL,status TEXT NOT NULL,error TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS pending_symbols(path TEXT NOT NULL,name TEXT NOT NULL,scope TEXT NOT NULL,kind TEXT NOT NULL,line INTEGER NOT NULL,col INTEGER NOT NULL,detail TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS scanned(path TEXT PRIMARY KEY);
        COMMIT;")?;
    if version < 2 {
        c.execute_batch("BEGIN;
            ALTER TABLE symbols ADD COLUMN ascii_only INTEGER NOT NULL DEFAULT 0;
            CREATE INDEX symbol_ascii ON symbols(ascii_only);
            CREATE VIRTUAL TABLE symbol_search USING fts5(qualified, tokenize='trigram');
            INSERT INTO symbol_search(rowid,qualified) SELECT id,CASE WHEN scope='' THEN name ELSE scope||'::'||name END FROM symbols;
            CREATE TRIGGER symbol_insert AFTER INSERT ON symbols BEGIN
              INSERT INTO symbol_search(rowid,qualified) VALUES(new.id,CASE WHEN new.scope='' THEN new.name ELSE new.scope||'::'||new.name END);
            END;
            CREATE TRIGGER symbol_delete AFTER DELETE ON symbols BEGIN DELETE FROM symbol_search WHERE rowid=old.id; END;
            PRAGMA user_version=2;
            COMMIT;")?;
    }
    c.execute_batch("BEGIN;
      CREATE TABLE IF NOT EXISTS xrefs(path TEXT NOT NULL REFERENCES files(path) ON DELETE CASCADE,source TEXT NOT NULL,target TEXT NOT NULL,kind TEXT NOT NULL,line INTEGER NOT NULL,col INTEGER NOT NULL);
      CREATE INDEX IF NOT EXISTS xref_target ON xrefs(target,kind);
      CREATE INDEX IF NOT EXISTS xref_source ON xrefs(source,kind);
      CREATE INDEX IF NOT EXISTS xref_path ON xrefs(path);
      CREATE TABLE IF NOT EXISTS pending_xrefs(path TEXT NOT NULL,source TEXT NOT NULL,target TEXT NOT NULL,kind TEXT NOT NULL,line INTEGER NOT NULL,col INTEGER NOT NULL);
      PRAGMA user_version=3; COMMIT;")?;
    Ok(c)
}
pub fn status(c: &Connection) -> Result<Value> {
    let files: i64 = c.query_row("SELECT count(*) FROM files", [], |r| r.get(0))?;
    let symbols: i64 = c.query_row("SELECT count(*) FROM symbols", [], |r| r.get(0))?;
    let stale: i64 = c.query_row(
        "SELECT count(*) FROM files WHERE status!='ready'",
        [],
        |r| r.get(0),
    )?;
    let root: String = c
        .query_row("SELECT value FROM meta WHERE key='root'", [], |r| r.get(0))
        .unwrap_or_default();
    let generation: String = c
        .query_row("SELECT value FROM meta WHERE key='generation'", [], |r| {
            r.get(0)
        })
        .unwrap_or_default();
    Ok(
        json!({"event":"status","files":files,"symbols":symbols,"stale":stale,"root":root,"generation":generation,"xref":"syntactic"}),
    )
}

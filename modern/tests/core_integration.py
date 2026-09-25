"""Integration checks against the distributed parsers; no project files are modified."""
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import tempfile
import time
import unittest

BASE = Path(__file__).resolve().parents[1]
ENGINE = BASE / "core/target/release/sn-index.exe"
PARSERS = BASE.parent / "outputs/libexec/snavigator"


class IndexTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.helpers = tempfile.TemporaryDirectory(prefix="sn-parser-tests-")
        cls.fake_parsers = Path(cls.helpers.name)
        source = cls.fake_parsers / "fixture.c"
        source.write_text('#include <windows.h>\n#include <stdio.h>\n#include <stdlib.h>\nint main(void) { if (getenv("SN_TEST_HANG")) Sleep(5000); puts("malformed parser output"); return 0; }\n')
        subprocess.run(["gcc", str(source), "-o", str(cls.fake_parsers / "cbrowser.exe")], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.helpers.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sn-tests-")
        self.root = Path(self.temp.name) / "progetto à 日本"
        self.root.mkdir()
        self.db = self.root / ".sn-index/index.sqlite"
        (self.root / "sample.c").write_text("int counter;\nint add(int a, int b) { return a+b; }\n")
        (self.root / "script.py").write_text("def calculate(value):\n    return value + 1\n")
        (self.root / "script.tcl").write_text("proc configure {value} {return $value}\n")

    def tearDown(self):
        self.temp.cleanup()

    def command(self, *args, success=True, env=None):
        result = subprocess.run([str(ENGINE), *map(str, args)], capture_output=True, timeout=40, env=env)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        else:
            self.assertNotEqual(result.returncode, 0)
        return [json.loads(row) for row in result.stdout.splitlines() if row]

    def index_args(self, parsers=PARSERS):
        return ["index", "--root", self.root, "--db", self.db, "--parsers", parsers, "--jobs", "2"]

    def index(self, **kwargs):
        return self.command(*self.index_args(**kwargs))[-1]

    def query(self, pattern="", mode="literal", **kwargs):
        rows = self.command("query", "--db", self.db, "--pattern", pattern, "--mode", mode, **kwargs)
        return rows[-1] if rows else None

    def generation(self):
        return self.command("status", "--db", self.db)[0]["generation"]

    def test_symbols_incremental_update_and_removal(self):
        self.assertEqual(self.index()["parsed"], 3)
        self.assertEqual({x["name"] for x in self.query()["results"]}, {"counter", "add", "calculate", "configure"})
        self.assertEqual(self.index()["unchanged"], 3)
        (self.root / "sample.c").write_text("int replacement;\n")
        result = self.index()
        self.assertEqual(result["parsed"], 1)
        self.assertEqual(result["unchanged"], 2)
        self.assertFalse(self.query("counter")["results"])
        self.assertTrue(self.query("replacement")["results"])
        (self.root / "script.py").unlink()
        self.index()
        self.assertFalse(self.query("calculate")["results"])
        self.assertEqual(self.command("check", "--db", self.db)[0]["result"], "ok")

    def test_wildcard_regex_and_bad_patterns(self):
        self.index()
        self.assertEqual(len(self.query("c*", "glob")["results"]), 3)
        self.assertEqual(len(self.query("^(add|calculate)$", "regex")["results"]), 2)
        self.assertFalse(self.query("c*", "literal")["results"])
        self.query("[", "regex", success=False)

    def test_search_current_source_text(self):
        self.index()
        (self.root / "sample.c").write_text("\ufeff// new comment\nint counter;\n",encoding="utf-8")
        results=self.command("grep","--db",self.db,"--pattern","new comment")[0]
        self.assertEqual(len(results["results"]),1)
        self.assertEqual(results["results"][0]["line"],1)
        self.assertEqual(results["results"][0]["column"],3)
        results=self.command("grep","--db",self.db,"--pattern","^int .*;","--mode","regex","--path","*.c")[0]
        self.assertEqual(results["results"][0]["line"],2)

    def test_parser_failure_preserves_old_symbols_and_retries(self):
        self.index()
        (self.root / "sample.c").write_text("int replacement;\n")
        failed = self.index(parsers=self.root / "missing-parsers")
        self.assertEqual(failed["errors"], 3)
        old = self.query("counter")["results"]
        self.assertEqual(old[0]["status"], "stale")
        refs=self.command("xref","--db",self.db,"--subject","counter","--relation","declaration")[0]["results"]
        self.assertTrue(refs)
        self.assertTrue(all(r["status"]=="stale" for r in refs))
        recovered = self.index()
        self.assertEqual(recovered["errors"], 0)
        self.assertTrue(self.query("replacement")["results"])
        logs = list(self.db.parent.joinpath("logs").glob("*.jsonl"))
        events = [json.loads(line) for p in logs for line in p.read_text().splitlines()]
        self.assertTrue(any(e["event"] == "parser_error" for e in events))
        self.assertTrue(all("run_id" in e and "time_unix_ms" in e for e in events))

    def test_cancel_does_not_publish(self):
        self.index()
        previous = self.generation()
        cancel = self.root / ".sn-index/cancel"
        cancel.write_text("cancel")
        self.command(*self.index_args(), "--cancel-file", cancel, success=False)
        self.assertEqual(self.generation(), previous)
        cancel.unlink()
        self.assertEqual(self.index()["errors"], 0)

    def test_malformed_output_preserves_symbols(self):
        self.index()
        self.index(parsers=self.fake_parsers)
        self.assertEqual(self.query("counter")["results"][0]["status"], "stale")
        records = self.command("files", "--db", self.db)[0]["files"]
        error = next(f["error"] for f in records if f["path"] == "sample.c")
        self.assertIn("PAF", error)

    def test_parser_timeout_is_bounded_and_recoverable(self):
        self.index()
        start = time.perf_counter()
        result = self.command(*self.index_args(parsers=self.fake_parsers), "--timeout-ms", "100", env=dict(os.environ, SN_TEST_HANG="1"))
        self.assertLess(time.perf_counter() - start, 4)
        self.assertEqual(result[-1]["errors"], 3)
        records = self.command("files", "--db", self.db)[0]["files"]
        self.assertIn("Timeout", next(f["error"] for f in records if f["path"] == "sample.c"))
        self.assertEqual(self.index()["errors"], 0)

    def test_kill_restart_and_writer_lock(self):
        self.index()
        previous = self.generation()
        for i in range(60):
            (self.root / f"more{i}.c").write_text(f"int variable{i};\n")
        proc = subprocess.Popen([str(ENGINE), *map(str, self.index_args())], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            while True:
                line = proc.stdout.readline()
                self.assertTrue(line)
                if json.loads(line)["event"] == "scan":
                    break
            self.command(*self.index_args(), success=False)
            proc.kill()
            proc.communicate(timeout=10)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.communicate(timeout=10)
        self.assertEqual(self.generation(), previous)
        resumed = self.command(*self.index_args())
        self.assertEqual(resumed[0]["recovered_runs"], 1)
        self.assertEqual(resumed[-1]["errors"], 0)
        self.assertEqual(self.command("check", "--db", self.db)[0]["result"], "ok")


if __name__ == "__main__":
    unittest.main(verbosity=2)

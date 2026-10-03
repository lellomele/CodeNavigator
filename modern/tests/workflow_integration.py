"""Search, replacement and recovery against real files in temporary projects."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

BASE = Path(__file__).resolve().parents[1]
ENGINE = BASE / 'core/target/release/sn-index.exe'
PARSERS = BASE.parent / 'outputs/libexec/snavigator'

class FileSearchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='code-nav-files-')
        self.root = Path(self.temp.name) / 'Sorgenti à 日本'
        for directory in ('src/nested', 'build', '.git', '.sn-index'):
            (self.root / directory).mkdir(parents=True, exist_ok=True)
        for name in ('src/nested/Widget.CPP', 'another.cpp', 'build/generated.cpp',
                     '.git/hidden.cpp', '.sn-index/internal.cpp', 'Makefile'):
            (self.root / name).write_text('// sample', encoding='utf-8')
        (self.root / 'binary.bin').write_bytes(b'\0\xff')

    def tearDown(self):
        self.temp.cleanup()

    def call(self, *args, ok=True):
        result = subprocess.run([str(ENGINE), 'find-files', '--root', str(self.root),
                                 *map(str, args)], capture_output=True, timeout=20)
        if ok:
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
            return json.loads(result.stdout)
        self.assertNotEqual(result.returncode, 0)

    def test_names_wildcards_regex_case_and_relative_paths(self):
        result = self.call('--pattern', '*.cpp')
        self.assertEqual({r['path'] for r in result['results']},
                         {'src/nested/Widget.CPP', 'another.cpp', 'build/generated.cpp'})
        self.assertFalse(result['truncated'])
        result = self.call('--pattern', '*.cpp', '--case-sensitive')
        self.assertEqual(len(result['results']), 2)
        result = self.call('--pattern', 'src\\*Widget.*')
        self.assertEqual([r['path'] for r in result['results']], ['src/nested/Widget.CPP'])
        result = self.call('--pattern', r'^(Widget|another)\.', '--mode', 'regex')
        self.assertEqual(len(result['results']), 2)
        result = self.call('--pattern', 'widget', '--mode', 'literal', '--path', 'src/*')
        self.assertEqual(len(result['results']), 1)

    def test_live_files_and_persistent_exclusions_without_an_index(self):
        result = self.call()
        self.assertIn('binary.bin', {r['path'] for r in result['results']})
        self.assertFalse(any('.git/' in r['path'] or '.sn-index/' in r['path'] for r in result['results']))
        result = self.call('--exclude-extension', 'cpp', '--exclude-extension', '@makefile')
        self.assertEqual([r['path'] for r in result['results']], ['binary.bin'])
        (self.root / 'new.abc').write_text('unindexed', encoding='utf-8')
        self.assertEqual(len(self.call('--pattern', '*.abc')['results']), 1)
        (self.root / 'new.abc').unlink()
        self.assertEqual(self.call('--pattern', '*.abc')['results'], [])
        self.assertFalse((self.root / 'index.sqlite').exists())

    def test_limits_and_invalid_expressions(self):
        result = self.call('--pattern', '*.cpp', '--limit', 1)
        self.assertTrue(result['truncated'])
        self.assertEqual(len(result['results']), 1)
        self.call('--pattern', '[invalid', '--mode', 'regex', ok=False)

class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='navigator-flow-')
        self.root = Path(self.temp.name) / 'Sorgenti à 日本'
        self.root.mkdir()
        self.db = Path(self.temp.name) / 'cache/index.sqlite'
        self.a = self.root / 'app.rs'
        self.b = self.root / 'other.ts'
        self.a.write_bytes(b'\xef\xbb\xbflet Alpha = "alpha";\r\n// alpha\r\n')
        self.b.write_text('const alpha = 1;\n', encoding='utf-8')
        self.index()

    def tearDown(self):
        self.temp.cleanup()

    def call(self, *args, ok=True):
        p = subprocess.run([str(ENGINE), *map(str,args)], capture_output=True, timeout=45)
        if ok:
            self.assertEqual(p.returncode, 0, p.stderr.decode(errors='replace'))
        else:
            self.assertNotEqual(p.returncode, 0)
        return [json.loads(x) for x in p.stdout.splitlines() if x]

    def index(self, *args):
        return self.call('index', '--root', self.root, '--db', self.db, '--parsers', PARSERS, *args)[-1]

    def plan(self, files=('app.rs',), pattern='alpha', replacement='beta', mode='literal', sensitive=False, ok=True, whole_words=False):
        req = Path(self.temp.name) / 'request.json'
        req.write_text(json.dumps(dict(db=str(self.db),files=list(files),pattern=pattern,replacement=replacement,mode=mode,case_sensitive=sensitive,whole_words=whole_words)),encoding='utf-8')
        result = self.call('replace-plan','--request',req,ok=ok)
        return result[-1] if result else None

    def test_xrefs_incremental_and_dependencies(self):
        self.a.write_text('fn target() {}\nfn caller() { let value = 1; target(); value += 2; }\n', encoding='utf-8')
        self.b.write_text('import "./base";\nclass Child extends Parent { run() { target(); } }\n', encoding='utf-8')
        self.index()
        def query(subject, relation, direction='incoming'):
            return self.call('xref','--db',self.db,'--subject',subject,'--relation',relation,'--direction',direction)[0]['results']
        calls=query('target','calls')
        self.assertEqual({r['source'] for r in calls},{'caller','run'})
        self.assertTrue(all(r['confidence']=='syntactic' for r in calls))
        self.assertEqual(query('caller','calls','outgoing')[0]['target'],'target')
        self.assertEqual(query('Parent','inherits')[0]['source'],'Child')
        self.assertEqual(query('./base','depends')[0]['path'],'other.ts')
        self.assertTrue(query('value','write'))
        self.assertTrue(query('value','read'))
        self.assertTrue(query('value','declaration'))
        self.assertEqual(self.index()['unchanged'],2)
        self.a.write_text('fn caller() { other(); }',encoding='utf-8')
        self.b.unlink()
        self.index()
        self.assertFalse(query('target','calls'))
        self.assertFalse(query('Parent','inherits'))
        self.assertEqual(query('other','calls')[0]['source'],'caller')

    def test_xref_parser_failure_preserves_previous_generation(self):
        self.a.write_text('fn caller() { target(); }',encoding='utf-8')
        self.index()
        self.a.write_text('x; '*260000,encoding='utf-8')
        result=self.index()
        self.assertEqual(result['errors'],1)
        refs=self.call('xref','--db',self.db,'--subject','target','--relation','calls')[0]['results']
        self.assertEqual(refs[0]['source'],'caller')
        self.assertEqual(refs[0]['status'],'stale')
        self.a.write_text('fn caller() { recovered(); }',encoding='utf-8')
        self.assertEqual(self.index()['errors'],0)
        self.assertFalse(self.call('xref','--db',self.db,'--subject','target','--relation','calls')[0]['results'])

    def test_print_preview_is_read_only_and_keeps_unchanged_matches(self):
        before=self.a.read_bytes()
        request=Path(self.temp.name)/'print.json'
        request.write_text(json.dumps(dict(db=str(self.db),files=['app.rs'],pattern='alpha',replacement='alpha',mode='literal',case_sensitive=True,preview_only=True)),encoding='utf-8')
        result=self.call('replace-plan','--request',request)[0]
        self.assertEqual(result['event'],'print_preview')
        self.assertEqual(result['entries'][0]['count'],2)
        self.assertEqual(result['entries'][0]['preview'][0]['before'],result['entries'][0]['preview'][0]['after'])
        self.assertNotIn('journal',result)
        self.assertFalse((self.db.parent/'changes').exists())
        self.assertEqual(self.a.read_bytes(),before)

    def test_discovery_counts_extensions_and_excludes_binary(self):
        (self.root/'fake.cpp').write_bytes(b'\x00\xff')
        (self.root/'custom.abc').write_text('sample')
        (self.root/'.git').mkdir()
        (self.root/'.git/hidden.py').write_text('ignored')
        result = self.call('discover','--root',self.root)[0]
        ext = {e['extension']:e for e in result['extensions']}
        self.assertTrue(ext['rs']['selected'])
        self.assertFalse(ext['abc']['selected'])
        self.assertNotIn('cpp',ext)
        self.assertNotIn('py',ext)

    def test_extension_selection_and_incremental_text_files(self):
        self.assertEqual(self.index()['unchanged'],2)
        self.index('--extensions','rs')
        self.assertEqual([x['path'] for x in self.call('files','--db',self.db)[0]['files']],['app.rs'])

    def test_search_all_occurrences_case_and_path(self):
        base = ['grep','--db',self.db,'--pattern','alpha']
        self.assertEqual(len(self.call(*base)[0]['results']),4)
        self.assertEqual(len(self.call(*base,'--case-sensitive','--path','*.rs')[0]['results']),2)
        hit=self.call(*base)[0]['results'][0]
        self.assertEqual(hit['column'],4)
        self.assertEqual(hit['length'],5)

    def test_whole_words_search_and_regex_replacement(self):
        self.a.write_text('alpha alphabet _alpha alpha2 éalpha Alpha alpha\n', encoding='utf-8')
        for mode, pattern in [('literal','alpha'), ('glob','al?ha'), ('regex','(al)(pha)')]:
            result=self.call('grep','--db',self.db,'--path','*.rs','--pattern',pattern,'--mode',mode,'--whole-words')[0]
            self.assertEqual(len(result['results']),3)
            result=self.call('grep','--db',self.db,'--path','*.rs','--pattern',pattern,'--mode',mode,'--whole-words','--case-sensitive')[0]
            self.assertEqual(len(result['results']),2)
        plan=self.plan(pattern='(al)(pha)', replacement='$2$1', mode='regex', sensitive=True, whole_words=True)
        self.assertEqual(plan['entries'][0]['count'],2)
        self.call('replace-apply','--journal',plan['journal'])
        self.assertEqual(self.a.read_text(encoding='utf-8'),'phaal alphabet _alpha alpha2 éalpha Alpha phaal\n')

    def test_plan_does_not_write_and_applies_selected_only(self):
        before=self.a.read_bytes();other=self.b.read_bytes()
        plan=self.plan()
        self.assertEqual(self.a.read_bytes(),before)
        self.assertEqual(plan['entries'][0]['count'],3)
        self.call('replace-apply','--journal',plan['journal'])
        self.assertEqual(self.a.read_bytes(),b'\xef\xbb\xbflet beta = "beta";\r\n// beta\r\n')
        self.assertEqual(self.b.read_bytes(),other)
        self.call('replace-undo','--journal',plan['journal'])
        self.assertEqual(self.a.read_bytes(),before)

    def test_conflict_in_second_file_prevents_all_writes(self):
        before=self.a.read_bytes()
        plan=self.plan(files=('app.rs','other.ts'))
        self.b.write_text('external change')
        self.call('replace-apply','--journal',plan['journal'],ok=False)
        self.assertEqual(self.a.read_bytes(),before)
        self.assertEqual(self.b.read_text(),'external change')

    def test_regex_capture_and_literal_dollar(self):
        plan=self.plan(pattern='(?P<word>alpha)',replacement='${word}_${1}_$$',mode='regex',sensitive=True)
        self.call('replace-apply','--journal',plan['journal'])
        self.assertIn(b'alpha_alpha_$',self.a.read_bytes())
        self.assertIn(b'Alpha',self.a.read_bytes())

    def test_unknown_capture_rejected(self):
        self.plan(pattern='(alpha)',replacement='$2',mode='regex',ok=False)

    def test_wildcards_in_text_use_substring_semantics(self):
        result=self.call('grep','--db',self.db,'--pattern','al?ha','--mode','glob')[0]
        self.assertEqual(len(result['results']),4)
        plan=self.plan(pattern='al?ha',mode='glob')
        self.call('replace-apply','--journal',plan['journal'])
        self.assertNotIn(b'alpha',self.a.read_bytes())

    def test_undo_refuses_later_external_edit(self):
        plan=self.plan();self.call('replace-apply','--journal',plan['journal'])
        self.a.write_text('new work')
        self.call('replace-undo','--journal',plan['journal'],ok=False)
        self.assertEqual(self.a.read_text(),'new work')

    def test_recovery_after_write_before_journal_commit(self):
        before=self.a.read_bytes();plan=self.plan(files=('app.rs','other.ts'))
        path=Path(plan['journal']);journal=json.loads(path.read_text(encoding="utf-8"))
        journal['state']='applying';path.write_text(json.dumps(journal),encoding="utf-8")
        # Simulate process death after the first atomic replacement, before its state update.
        self.a.write_bytes((path.parent/'0.after').read_bytes())
        self.call('replace-undo','--journal',path)
        self.assertEqual(self.a.read_bytes(),before)
        self.assertEqual(json.loads(path.read_text(encoding="utf-8"))['state'],'restored')

    def test_corrupt_backup_blocks_write(self):
        before=self.a.read_bytes();plan=self.plan();path=Path(plan['journal'])
        (path.parent/'0.before').write_text('damaged')
        self.call('replace-apply','--journal',path,ok=False)
        self.assertEqual(self.a.read_bytes(),before)

    def test_empty_replacement_removes_matches(self):
        plan=self.plan(replacement='');self.call('replace-apply','--journal',plan['journal'])
        self.assertEqual(self.a.read_bytes(),b'\xef\xbb\xbflet  = "";\r\n// \r\n')

    def test_traversal_and_nonindexed_files_rejected(self):
        self.plan(files=('../outside.rs',),ok=False)
        (self.root/'secret.abc').write_text('alpha')
        self.plan(files=('secret.abc',),ok=False)

    def test_repeated_apply_rejected_and_undo_idempotent(self):
        before=self.a.read_bytes();plan=self.plan()
        self.call('replace-apply','--journal',plan['journal'])
        self.call('replace-apply','--journal',plan['journal'],ok=False)
        self.call('replace-undo','--journal',plan['journal'])
        self.call('replace-undo','--journal',plan['journal'])
        self.assertEqual(self.a.read_bytes(),before)

    def test_utf16_and_legacy_encoding_preserved(self):
        for encoding in ('utf-16', 'cp1252'):
            before='// caffè alpha\r\n'.encode(encoding)
            self.a.write_bytes(before)
            self.index()
            plan=self.plan(replacement='β' if encoding=='utf-16' else 'città')
            self.call('replace-apply','--journal',plan['journal'])
            expected='// caffè '+('β' if encoding=='utf-16' else 'città')+'\r\n'
            self.assertEqual(self.a.read_bytes(),expected.encode(encoding))
            self.call('replace-undo','--journal',plan['journal'])
            self.assertEqual(self.a.read_bytes(),before)

    def test_unrepresentable_replacement_never_changes_file(self):
        before='// caffè alpha'.encode('cp1252');self.a.write_bytes(before);self.index()
        self.plan(replacement='日本',ok=False)
        self.assertEqual(self.a.read_bytes(),before)

    def test_binary_replacement_is_rejected_before_write(self):
        before=self.a.read_bytes();self.plan(replacement='\x00',ok=False)
        self.assertEqual(self.a.read_bytes(),before)

if __name__=='__main__':unittest.main(verbosity=2)

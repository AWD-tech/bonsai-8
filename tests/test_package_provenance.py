"""A pinned source package can coexist with unrelated website work."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('bonsai_package',ROOT/'tools/package.py')
package=importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)

class ProvenanceTests(unittest.TestCase):
    def test_only_committed_identical_source_can_be_clean(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            def git(*args):
                subprocess.run(['git',*args],cwd=root,check=True,capture_output=True)
            git('init')
            source=root/'firmware.c';source.write_text('reviewed source\n')
            git('add','firmware.c')
            git('-c','user.name=Test','-c','user.email=test@example.invalid',
                'commit','-m','Reviewed source')
            self.assertFalse(package.source_is_dirty(root,[source]))
            (root/'site.html').write_text('unrelated work in progress')
            self.assertFalse(package.source_is_dirty(root,[source]))
            source.write_text('uncommitted firmware change\n')
            self.assertTrue(package.source_is_dirty(root,[source]))
            new=root/'new.c';new.write_text('not committed')
            self.assertTrue(package.source_is_dirty(root,[new]))

if __name__=='__main__':unittest.main()

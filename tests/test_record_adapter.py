"""Exercise actual recording adapter functions against synthetic, file-backed IO.

No serial ports, firmware builds, real recordings or hardware timing are used.
The native fixture also supplies the site's real DeviceLibrary.export path.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def declaration(source, start, terminator="}"):
    match = re.search(start, source, re.M)
    if not match:
        raise AssertionError("Missing production declaration: " + start)
    brace = source.index("{", match.start())
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + (";" if terminator == ";" else "")


def function(source, name):
    return declaration(source, r"^static [^\n]*\b" + name + r"\s*\([^;]*?\)\s*(?:/\*.*?\*/\s*)?\{")


class RecordingAdapterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="bonsai-record-host-")
        adapter = (ROOT / "firmware/src/dual_firmware.inc").read_text()
        main = (ROOT / "firmware/src/main.c").read_text()
        types = "\n".join(declaration(main, r"^struct " + name + r" \{", ";")
                          for name in ("slot_state", "meta_blk", "x3_trk", "x3_tab"))
        constants = "\n".join(re.search(r"^#define " + name + r"\s+[^\n]*", main, re.M)[0].split("/*")[0]
                              for name in ("BEAT_SAMPLES_I2S", "BEAT_SAMPLES_L", "MAX_BEATS", "MAX_LOOP_SAMPLES"))
        funcs = [function(main, name) for name in ("trk_blk", "x3_sum", "x3_valid")]
        funcs += [function(adapter, name) for name in
                  ("dual_record_busy", "dual_audio_block", "dual_read_index", "dual_song_present",
                   "dual_checksum", "dual_record_failed", "dual_write_checked",
                   "dual_record_publish", "dual_record_service")]
        harness = (ROOT / "tests/helpers/record_adapter.c").read_text()
        harness = harness.replace("/* PRODUCTION_TYPES */", constants + "\n" + types)
        harness = harness.replace("/* PRODUCTION_FUNCTIONS */", "\n".join(funcs))
        src = Path(cls.tmp.name) / "record_adapter.c"
        src.write_text(harness)
        cls.exe = Path(cls.tmp.name) / "record_adapter"
        subprocess.run(["cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-I", str(ROOT / "firmware/src"), str(src),
                        *[str(ROOT / "firmware/src" / name) for name in
                          ("bonsai_record.c", "bonsai_library.c", "bonsai_playlist.c", "dual_engine.c", "bonsai_stretch.c")],
                        "-lm", "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_case(self, name, *args):
        result = subprocess.run([str(self.exe), name, *map(str, args)], capture_output=True,
                                text=True, timeout=180)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        return result.stdout

    def test_pause_fade_save_reload_and_exact_stereo(self):
        self.run_case("save")

    def test_every_save_io_failure_and_final_publication_limit(self):
        self.run_case("failures")

    def test_overflow_write_failure_and_corruption_never_publish(self):
        self.run_case("guards")

    def test_empty_full_invalid_capacity_and_restart(self):
        self.run_case("admission")

    def test_maximum_take_eight_stem_render_and_automatic_stop(self):
        with tempfile.TemporaryDirectory(dir=self.tmp.name) as directory:
            self.run_case("maximum", directory)
            self.export_fixture(directory)

    def export_fixture(self, directory):
        result = subprocess.run(["node", str(ROOT / "tests/helpers/record_export.mjs"), directory],
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)

    def test_actual_saved_recording_exports_exact_site_wav(self):
        with tempfile.TemporaryDirectory(dir=self.tmp.name) as directory:
            self.run_case("fixture", directory)
            self.export_fixture(directory)


if __name__ == "__main__":
    unittest.main()

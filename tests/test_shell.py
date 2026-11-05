"""Build and run the actual LVGL shell, substituting only device/data services."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class ShellTests(unittest.TestCase):
    def test_lvgl_shell(self):
        with tempfile.TemporaryDirectory() as temp:
            # Keep full diagnostics on failure without flooding normal test output.
            commands = [
                ['cmake', '-S', str(ROOT / 'tests/host'), '-B', temp],
                ['cmake', '--build', temp, '-j', '8'],
                [str(Path(temp) / 'shell_test'), temp],
            ]
            for command in commands:
                result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
                self.assertEqual(result.returncode, 0, result.stdout[-10000:])
                if command[0].endswith("shell_test") and result.stdout:print(result.stdout.strip())

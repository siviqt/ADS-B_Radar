import pathlib
import subprocess
import sys
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / 'tools/check_image.py'

class ImageGate(unittest.TestCase):
    def check(self, size, free=None, magic=0xE9):
        with tempfile.TemporaryDirectory() as directory:
            image = pathlib.Path(directory) / 'app.bin'
            image.write_bytes(bytes([magic]) + bytes(size-1))
            args = [sys.executable, str(SCRIPT), str(image)]
            if free is not None:
                args += ['--free-bytes', str(free)]
            result = subprocess.run(args, capture_output=True, text=True)
            return result.returncode, result.stdout

    def test_rounding_and_exact_space(self):
        code, text = self.check(65537, 131072)
        self.assertEqual(code, 0)
        self.assertIn('"appfs_allocation_bytes": 131072', text)

    def test_insufficient_space(self):
        self.assertNotEqual(self.check(65537, 131071)[0], 0)

    def test_limit(self):
        self.assertEqual(self.check(2097152)[0], 0)
        self.assertNotEqual(self.check(2097153)[0], 0)

    def test_invalid_magic(self):
        self.assertNotEqual(self.check(128, magic=0)[0], 0)

if __name__ == '__main__':
    unittest.main()

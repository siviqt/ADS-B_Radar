"""Exercise the production SD lifecycle controller with failing BSP operations."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StorageTests(unittest.TestCase):
    def test_lifecycle(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = str(Path(temp) / 'storage')
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I', str(ROOT / 'tests/storage_stubs'),
                            '-I', str(ROOT / 'main'), str(ROOT / 'tests/storage.cpp'),
                            '-o', binary], check=True)
            subprocess.run([binary], env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'), check=True)

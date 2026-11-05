"""Exercise the portable navigation model with memory/undefined behavior checks."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class NavigationTests(unittest.TestCase):
    def test_navigation(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = str(Path(temp) / 'navigation')
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I', str(ROOT / 'main'),
                            str(ROOT / 'tests/navigation.cpp'), '-o', binary], check=True)
            subprocess.run([binary], env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'), check=True)

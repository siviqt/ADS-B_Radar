"""Compile the actual portable service model and replay it with memory checks."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class DataTests(unittest.TestCase):
    def test_replay(self):
        includes = ROOT / 'managed_components/bblanchon__arduinojson/src'
        self.assertTrue(includes.is_dir(), 'Run the IDF build to resolve pinned ArduinoJson first')
        with tempfile.TemporaryDirectory() as temp:
            binary = str(Path(temp) / 'model')
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I', str(ROOT / 'main'),
                            '-I', str(includes), str(ROOT / 'tests/model.cpp'), '-o', binary], check=True)
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
            # LeakSanitizer cannot run under the desktop sandbox's ptrace.
            subprocess.run([binary, str(ROOT / 'tests/fixtures/aircraft.json')], env=env, check=True)

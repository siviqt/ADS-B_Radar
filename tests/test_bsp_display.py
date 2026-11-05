"""Exercise the complete patched BSP display implementation with failing panel calls."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class BspDisplayTests(unittest.TestCase):
    def test_initialization_errors(self):
        source=ROOT/'compat/badge_bsp_display.c'
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)
            # Substitute external hardware headers; compile the actual BSP source.
            for name in re.findall(r'^#include "([^"]+)"',source.read_text(),re.M):
                header=folder/name;header.parent.mkdir(parents=True,exist_ok=True)
                header.write_text('#include "platform.h"\n')
            binary=folder/'bsp_display'
            result=subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                '-Wno-unused-parameter','-Wno-unused-variable','-fsanitize=address,undefined',
                '-I',str(folder),'-I',str(ROOT/'tests/display_stubs'),str(source),
                str(ROOT/'tests/bsp_display.c'),'-o',str(binary)],text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            subprocess.run([str(binary)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))

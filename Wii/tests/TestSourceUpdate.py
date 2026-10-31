#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Settings/browser regressions and release packaging without a Wii SDK."""
from pathlib import Path
import importlib.util
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('wii_release', root / 'Wii/build_release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='release-regression-', dir=root)
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.output = self.base / 'release.zip'
        self.output.write_bytes(b'previous release')
        self.dol = self.base / 'boot.dol'
        data = bytearray(0x104)
        for offset, value in [(0, 0x100), (0x48, 0x80004000), (0x90, 4), (0xe0, 0x80004000)]:
            struct.pack_into('>I', data, offset, value)
        self.dol.write_bytes(data)

    def test_complete_archive(self):
        release.package_release(root, self.dol, self.output)
        with zipfile.ZipFile(self.output) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(archive.read('apps/ppsspp-wii/boot.dol'), self.dol.read_bytes())
            for name in ['CURRENT-UPDATE.md', 'DEVELOPMENT-NOTES.md', 'LICENSE.TXT', 'FONT-LICENSE.txt']:
                self.assertIn('apps/ppsspp-wii/' + name, archive.namelist())
        self.assertFalse(list(self.base.glob('*.tmp')))

    def test_missing_input_preserves_previous_release(self):
        with self.assertRaises(FileNotFoundError):
            release.package_release(self.base, self.dol, self.output)
        self.assertEqual(self.output.read_bytes(), b'previous release')

    def test_asset_failure_preserves_previous_release(self):
        original = zipfile.ZipFile.write

        def fail_asset(archive, filename, *args, **kwargs):
            if Path(filename).is_relative_to(root / 'assets'):
                raise OSError('simulated asset read failure')
            return original(archive, filename, *args, **kwargs)

        with patch.object(zipfile.ZipFile, 'write', fail_asset):
            with self.assertRaises(OSError):
                release.package_release(root, self.dol, self.output)
        self.assertEqual(self.output.read_bytes(), b'previous release')
        self.assertFalse(list(self.base.glob('*.tmp')))

    def test_invalid_dol_preserves_previous_release(self):
        self.dol.write_bytes(b'truncated')
        with self.assertRaises(RuntimeError):
            release.package_release(root, self.dol, self.output)
        self.assertEqual(self.output.read_bytes(), b'previous release')


def check_cpp():
    with tempfile.TemporaryDirectory(prefix='source-update-', dir=root) as folder:
        folder = Path(folder)
        for name, sources, extra in [
            ('settings', ['Wii/Settings.cpp', 'Wii/tests/SettingsTests.cpp'], []),
            ('settings-recovery', ['Wii/Settings.cpp', 'Wii/tests/SettingsRecoveryTests.cpp'], ['-Wl,--wrap=rename']),
            ('browser', ['Wii/DiscBrowser.cpp', 'Wii/tests/BrowserTests.cpp'], []),
        ]:
            exe = folder / name
            subprocess.run(['g++', '-std=c++11', '-O2', '-fsanitize=undefined',
                            '-fno-sanitize-recover=all', '-I' + str(root),
                            *sources, *extra, '-o', str(exe)], cwd=root, check=True)
            subprocess.run([str(exe)], cwd=folder, check=True)


if __name__ == '__main__':
    check_cpp()
    unittest.main()

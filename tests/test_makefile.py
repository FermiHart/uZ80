#!/usr/bin/env python3
# SPDX-License-Identifier: Unlicense
"""Behavioral tests for destructive and ownership-sensitive Make targets."""

from pathlib import Path
import subprocess
import tempfile
import unittest

from tests.test_validate_build import BuildFixture


ROOT = Path(__file__).resolve().parents[1]


class MakefileTests(unittest.TestCase):
    def run_make(
        self, target: str, build: Path, *variables: str
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            (
                "make", target, f"BUILD={build}",
                "Z80_CC=true", "Z80_AS=true", "MAKEBIN=true",
                *variables,
            ),
            cwd=ROOT,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )

    def run_clean(self, build: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ("make", "clean", f"BUILD={build}"),
            cwd=ROOT,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )

    def test_clean_refuses_unowned_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary) / "foreign"
            build.mkdir()
            sentinel = build / "keep-me"
            sentinel.write_text("not a uZ80 build\n", encoding="ascii")

            result = self.run_clean(build)

            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertTrue(sentinel.exists())

    def test_clean_removes_build_owned_by_this_checkout(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary) / "owned"
            build.mkdir()
            (build / ".uz80-build").write_text(
                str(ROOT.resolve()) + "\n", encoding="utf-8"
            )
            (build / "artifact").write_bytes(b"generated")

            result = self.run_clean(build)

            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertFalse(build.exists())

    def test_prepare_build_rechecks_existing_marker_owner(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary) / "foreign"
            build.mkdir()
            (build / ".uz80-build").write_text(
                "/a/different/checkout\n", encoding="utf-8"
            )

            result = self.run_make("prepare-build", build)

            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("another checkout", result.stdout)

    def test_host_test_disables_sanitizer_recovery(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            result = subprocess.run(
                (
                    "make", "--dry-run", "host-test",
                    f"BUILD={Path(temporary) / 'build'}", "HOST_CC=cc",
                ),
                cwd=ROOT,
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )

            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn("-fno-sanitize-recover=all", result.stdout)

    def test_all_revalidates_an_existing_primary_rom(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary) / "owned"
            build.mkdir()
            (build / ".uz80-build").write_text(
                str(ROOT.resolve()) + "\n", encoding="utf-8"
            )
            fixture = BuildFixture(build)

            valid = self.run_make("all", build)
            self.assertEqual(valid.returncode, 0, valid.stdout)

            rom = bytearray(fixture.rom_path.read_bytes())
            rom[0x48] ^= 0xFF
            fixture.rom_path.write_bytes(rom)
            invalid = self.run_make("all", build)

            self.assertNotEqual(invalid.returncode, 0, invalid.stdout)
            self.assertIn("does not match", invalid.stdout)

    def test_stage_revalidates_and_replaces_the_emulator_rom(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "owned"
            keymaps = root / "keymaps"
            build.mkdir()
            keymaps.mkdir()
            (keymaps / "en-us").write_text("fixture\n", encoding="ascii")
            (build / ".uz80-build").write_text(
                str(ROOT.resolve()) + "\n", encoding="utf-8"
            )
            fixture = BuildFixture(build)

            first = self.run_make("stage", build, f"KEYMAPS={keymaps}")
            self.assertEqual(first.returncode, 0, first.stdout)
            staged = build / "data" / "zx-rom.bin"
            self.assertEqual(staged.read_bytes(), fixture.rom_path.read_bytes())

            staged.write_bytes(b"stale")
            second = self.run_make("stage", build, f"KEYMAPS={keymaps}")
            self.assertEqual(second.returncode, 0, second.stdout)
            self.assertEqual(staged.read_bytes(), fixture.rom_path.read_bytes())


if __name__ == "__main__":
    unittest.main(verbosity=2)

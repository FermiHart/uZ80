#!/usr/bin/env python3
# SPDX-License-Identifier: Unlicense
"""Regression checks for the public repository surface and local links."""

from pathlib import Path
import hashlib
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
MARKDOWN_TARGET = re.compile(r"]\(([^)]+)\)")
HTML_TARGET = re.compile(r'(?:href|src)="([^"]+)"')


class PublicSurfaceTests(unittest.TestCase):
    def test_relative_documentation_links_resolve(self) -> None:
        failures: list[str] = []
        root = ROOT.resolve()
        for document in sorted(ROOT.rglob("*.md")):
            if ".git" in document.parts or "build" in document.parts:
                continue
            text = document.read_text(encoding="utf-8")
            targets = MARKDOWN_TARGET.findall(text) + HTML_TARGET.findall(text)
            for target in targets:
                if target.startswith(("#", "http://", "https://", "mailto:")):
                    continue
                path = target.split("#", 1)[0]
                if not path:
                    continue
                resolved = (document.parent / path).resolve()
                try:
                    resolved.relative_to(root)
                except ValueError:
                    failures.append(
                        f"{document.relative_to(ROOT)} -> {target} escapes repository"
                    )
                    continue
                if not resolved.exists():
                    failures.append(f"{document.relative_to(ROOT)} -> {target}")

        self.assertEqual(failures, [], "broken local links:\n" + "\n".join(failures))

    def test_public_configuration_contains_no_private_checkout_path(self) -> None:
        for relative in (
            "README.md",
            "ROADMAP.md",
            "Makefile",
            ".github/workflows/ci.yml",
        ):
            text = (ROOT / relative).read_text(encoding="utf-8")
            self.assertNotIn("/home/", text, relative)
            self.assertNotIn("work/qemu-z80", text, relative)

    def test_workflow_name_states_its_proof_boundary(self) -> None:
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(
            encoding="utf-8"
        )
        self.assertTrue(
            workflow.startswith("name: Source-to-ROM proof\n"),
            "workflow name must not imply emulator or hardware proof",
        )

    def test_screenshot_manifest_matches_versioned_images(self) -> None:
        directory = ROOT / "docs" / "screenshots"
        manifest = (directory / "SHA256SUMS").read_text(encoding="ascii")
        entries = [line.split() for line in manifest.splitlines() if line]
        filenames = [filename for _, filename in entries]
        curated = {
            path.name
            for pattern in ("*.jpg", "*.png")
            for path in directory.glob(pattern)
        }

        self.assertEqual(len(entries), 10)
        self.assertEqual(
            len(filenames), len(set(filenames)), "duplicate manifest entry"
        )
        self.assertEqual(set(filenames), curated, "manifest must cover curated images")
        for expected, filename in entries:
            self.assertEqual(
                Path(filename).name, filename, "manifest path must be local"
            )
            image = directory / filename
            actual = hashlib.sha256(image.read_bytes()).hexdigest()
            self.assertEqual(actual, expected, filename)


if __name__ == "__main__":
    unittest.main(verbosity=2)

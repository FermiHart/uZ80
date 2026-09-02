#!/usr/bin/env python3
# SPDX-License-Identifier: Unlicense
"""Regression tests for the fail-closed ROM and linker-map validator."""

from pathlib import Path
import tempfile
import unittest

from tools.validate_build import BuildValidationError, validate_build


ROM_SIZE = 0x4000


def ihex_record(address: int, data: bytes, record_type: int = 0) -> str:
    body = bytes((len(data), address >> 8, address & 0xFF, record_type)) + data
    checksum = (-sum(body)) & 0xFF
    return ":" + (body + bytes((checksum,))).hex().upper()


def linker_map(**overrides: int) -> str:
    symbols = {
        "s__HEADER0": 0,
        "l__HEADER0": 4,
        "s__HEADER1": 0,
        "l__HEADER1": 14,
        "s__CODE": 0x0048,
        "l__CODE": 4,
        "s__GSINIT": 0x004C,
        "l__GSINIT": 0,
        "s__GSFINAL": 0x004C,
        "l__GSFINAL": 0x0001,
        "s__DATA": 0x6010,
        "l__DATA": 0x0010,
        "s__INITIALIZED": 0x6020,
        "l__INITIALIZED": 0,
        "s__INITIALIZER": 0,
        "l__INITIALIZER": 0,
    }
    symbols.update(overrides)
    areas = {
        "_HEADER0": (0, symbols["l__HEADER0"]),
        "_HEADER1": (0, symbols["l__HEADER1"]),
        "_CODE": (symbols["s__CODE"], symbols["l__CODE"]),
        "_GSINIT": (symbols["s__GSINIT"], symbols["l__GSINIT"]),
        "_GSFINAL": (symbols["s__GSFINAL"], symbols["l__GSFINAL"]),
        "_DATA": (symbols["s__DATA"], symbols["l__DATA"]),
    }
    globals_text = "\n".join(
        f"     {value:08X}  {name}" for name, value in symbols.items()
    )
    areas_text = "\n".join(
        f"{name:<36}{start:08X}    {size:08X} = {size:11d}. bytes"
        for name, (start, size) in areas.items()
    )
    return globals_text + "\n" + areas_text + "\n"


class BuildFixture:
    def __init__(self, root: Path):
        self.map_path = root / "uz80.map"
        self.ihx_path = root / "uz80.ihx"
        self.rom_path = root / "uz80.rom"

        chunks = {
            0x0000: b"\xF3\xC3\x48\x00",
            0x0038: b"\xF5\xE5\x2A\x08\x60\x23\x22\x08\x60\xE1\xF1\xFB\xED\x4D",
            0x0048: b"UZ80",
            0x004C: b"\xC9",
        }
        self.write_map(linker_map())
        self.write_image(chunks)

    def write_map(self, text: str) -> None:
        self.map_path.write_text(text, encoding="ascii")

    def write_image(self, chunks: dict[int, bytes]) -> None:
        records = [ihex_record(address, data) for address, data in chunks.items()]
        records.append(ihex_record(0, b"", 1))
        self.ihx_path.write_text("\n".join(records) + "\n", encoding="ascii")

        rom = bytearray(b"\xFF" * ROM_SIZE)
        for address, data in chunks.items():
            if address < ROM_SIZE:
                end = min(address + len(data), ROM_SIZE)
                rom[address:end] = data[:end - address]
        self.rom_path.write_bytes(rom)


class ValidateBuildTests(unittest.TestCase):
    def fixture(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        return BuildFixture(Path(temporary.name))

    def test_accepts_consistent_bounded_build(self) -> None:
        fixture = self.fixture()
        report = validate_build(
            fixture.map_path, fixture.ihx_path, fixture.rom_path
        )
        self.assertEqual(report.rom_size, ROM_SIZE)
        self.assertEqual(report.data_end, 0x6020)

    def test_rejects_initialized_ram_without_crt_copy_support(self) -> None:
        fixture = self.fixture()
        fixture.write_map(linker_map(
            l__INITIALIZED=2, s__INITIALIZER=0x0200, l__INITIALIZER=2
        ))
        with self.assertRaisesRegex(BuildValidationError, "initialized data"):
            validate_build(fixture.map_path, fixture.ihx_path, fixture.rom_path)

    def test_rejects_any_intel_hex_byte_outside_the_rom(self) -> None:
        fixture = self.fixture()
        fixture.write_image({0x0000: b"boot", 0x4000: b"lost"})
        with self.assertRaisesRegex(BuildValidationError, "outside ROM"):
            validate_build(fixture.map_path, fixture.ihx_path, fixture.rom_path)

    def test_rejects_data_segment_collision_with_filesystem(self) -> None:
        fixture = self.fixture()
        fixture.write_map(linker_map(l__DATA=0x03F1, s__INITIALIZED=0x6401))
        with self.assertRaisesRegex(BuildValidationError, "filesystem"):
            validate_build(fixture.map_path, fixture.ihx_path, fixture.rom_path)

    def test_rejects_rom_that_differs_from_intel_hex(self) -> None:
        fixture = self.fixture()
        rom = bytearray(fixture.rom_path.read_bytes())
        rom[0x48] ^= 0xFF
        fixture.rom_path.write_bytes(rom)
        with self.assertRaisesRegex(BuildValidationError, "does not match"):
            validate_build(fixture.map_path, fixture.ihx_path, fixture.rom_path)

    def test_rejects_bad_intel_hex_checksum(self) -> None:
        fixture = self.fixture()
        lines = fixture.ihx_path.read_text(encoding="ascii").splitlines()
        lines[0] = lines[0][:-2] + "00"
        fixture.ihx_path.write_text("\n".join(lines) + "\n", encoding="ascii")
        with self.assertRaisesRegex(BuildValidationError, "checksum"):
            validate_build(fixture.map_path, fixture.ihx_path, fixture.rom_path)

    def test_rejects_eof_only_image(self) -> None:
        fixture = self.fixture()
        fixture.write_image({})
        with self.assertRaisesRegex(BuildValidationError, "reset vector"):
            validate_build(fixture.map_path, fixture.ihx_path, fixture.rom_path)

    def test_rejects_truncated_linked_area(self) -> None:
        fixture = self.fixture()
        fixture.write_image({
            0x0000: b"\xF3\xC3\x48\x00",
            0x0038: b"\x00" * 14,
            0x0048: b"UZ8",
            0x004C: b"\xC9",
        })
        with self.assertRaisesRegex(BuildValidationError, "_CODE.*missing"):
            validate_build(fixture.map_path, fixture.ihx_path, fixture.rom_path)

    def test_rejects_unknown_nonempty_linker_area(self) -> None:
        fixture = self.fixture()
        fixture.write_map(
            linker_map()
            + f"{'_SURPRISE':<36}{0x6200:08X}    {1:08X} = {1:11d}. bytes\n"
        )
        with self.assertRaisesRegex(BuildValidationError, "unsupported.*_SURPRISE"):
            validate_build(fixture.map_path, fixture.ihx_path, fixture.rom_path)

    def test_rejects_emitted_byte_not_owned_by_a_rom_area(self) -> None:
        fixture = self.fixture()
        fixture.write_image({
            0x0000: b"\xF3\xC3\x48\x00",
            0x0020: b"orphan",
            0x0038: b"\x00" * 14,
            0x0048: b"UZ80",
            0x004C: b"\xC9",
        })
        with self.assertRaisesRegex(BuildValidationError, "unclaimed"):
            validate_build(fixture.map_path, fixture.ihx_path, fixture.rom_path)

    def test_rejects_vector_only_image_with_empty_executable_areas(self) -> None:
        fixture = self.fixture()
        fixture.write_map(linker_map(
            l__CODE=0,
            s__GSINIT=0x0048,
            l__GSINIT=0,
            s__GSFINAL=0x0048,
            l__GSFINAL=0,
        ))
        fixture.write_image({
            0x0000: b"\xF3\xC3\x48\x00",
            0x0038: b"\x00" * 14,
        })
        with self.assertRaisesRegex(BuildValidationError, "_CODE.*empty"):
            validate_build(fixture.map_path, fixture.ihx_path, fixture.rom_path)


if __name__ == "__main__":
    unittest.main(verbosity=2)

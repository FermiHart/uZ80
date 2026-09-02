#!/usr/bin/env python3
# SPDX-License-Identifier: Unlicense
"""Validate that an SDCC build is a complete, bounded 16 KiB ROM."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys


ROM_SIZE = 0x4000
CODE_START = 0x0048
DATA_START = 0x6010
FILESYSTEM_START = 0x6400

_SYMBOL_RE = re.compile(
    r"^\s*([0-9A-Fa-f]{8})\s+([ls]__[A-Za-z0-9_]+)(?:\s|$)"
)
_AREA_RE = re.compile(
    r"^\s*([A-Za-z_][A-Za-z0-9_.]*)\s+"
    r"([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s+="
)
_REQUIRED_SYMBOLS = tuple(
    prefix + area
    for area in (
        "_CODE",
        "_GSINIT",
        "_GSFINAL",
        "_DATA",
        "_INITIALIZED",
        "_INITIALIZER",
        "_HEADER0",
        "_HEADER1",
    )
    for prefix in ("s_", "l_")
)
_EXPECTED_AREAS = {
    "_HEADER0", "_HEADER1", "_CODE", "_GSINIT", "_GSFINAL", "_DATA"
}


class BuildValidationError(ValueError):
    """The linked image violates a build or memory-layout contract."""


@dataclass(frozen=True)
class BuildReport:
    rom_size: int
    rom_used: int
    data_end: int


def _parse_map(path: Path) -> tuple[dict[str, int], dict[str, tuple[int, int]]]:
    symbols: dict[str, int] = {}
    areas: dict[str, tuple[int, int]] = {}
    for line in path.read_text(encoding="ascii").splitlines():
        match = _SYMBOL_RE.match(line)
        if match:
            value = int(match.group(1), 16)
            name = match.group(2)
            previous = symbols.setdefault(name, value)
            if previous != value:
                raise BuildValidationError(
                    f"linker symbol {name} has conflicting values"
                )
            continue
        match = _AREA_RE.match(line)
        if match:
            name = match.group(1)
            extent = (int(match.group(2), 16), int(match.group(3), 16))
            if name in areas:
                raise BuildValidationError(f"linker area {name} is duplicated")
            areas[name] = extent

    missing = [name for name in _REQUIRED_SYMBOLS if name not in symbols]
    if missing:
        raise BuildValidationError(
            "linker map is missing: " + ", ".join(missing)
        )
    missing_areas = sorted(_EXPECTED_AREAS - areas.keys())
    if missing_areas:
        raise BuildValidationError(
            "linker map is missing areas: " + ", ".join(missing_areas)
        )
    for name, (_, size) in areas.items():
        if size and name not in _EXPECTED_AREAS:
            raise BuildValidationError(f"unsupported nonempty linker area {name}")
    for name in _EXPECTED_AREAS:
        expected = (symbols["s_" + name], symbols["l_" + name])
        if areas[name] != expected:
            raise BuildValidationError(
                f"linker area {name} disagrees with its boundary symbols"
            )
    return symbols, areas


def _area_end(symbols: dict[str, int], area: str) -> int:
    start = symbols["s_" + area]
    size = symbols["l_" + area]
    end = start + size
    if end > 0x10000:
        raise BuildValidationError(f"{area} exceeds the Z80 address space")
    return end


def _parse_ihex(path: Path) -> dict[int, int]:
    memory: dict[int, int] = {}
    base = 0
    saw_eof = False

    for line_number, raw_line in enumerate(
            path.read_text(encoding="ascii").splitlines(), 1):
        line = raw_line.strip()
        if not line:
            continue
        if saw_eof:
            raise BuildValidationError("Intel HEX contains data after EOF")
        if not line.startswith(":"):
            raise BuildValidationError(
                f"Intel HEX line {line_number} has no record marker"
            )
        try:
            record = bytes.fromhex(line[1:])
        except ValueError as error:
            raise BuildValidationError(
                f"Intel HEX line {line_number} is not hexadecimal"
            ) from error
        if len(record) < 5 or len(record) != record[0] + 5:
            raise BuildValidationError(
                f"Intel HEX line {line_number} has an invalid length"
            )
        if sum(record) & 0xFF:
            raise BuildValidationError(
                f"Intel HEX line {line_number} has a bad checksum"
            )

        count = record[0]
        address = (record[1] << 8) | record[2]
        record_type = record[3]
        data = record[4:4 + count]

        if record_type == 0:
            absolute = base + address
            for offset, value in enumerate(data):
                location = absolute + offset
                previous = memory.setdefault(location, value)
                if previous != value:
                    raise BuildValidationError(
                        f"Intel HEX writes conflicting data at 0x{location:04X}"
                    )
        elif record_type == 1:
            if count or address:
                raise BuildValidationError("Intel HEX EOF record is malformed")
            saw_eof = True
        elif record_type == 2:
            if count != 2 or address:
                raise BuildValidationError(
                    "Intel HEX segment-address record is malformed"
                )
            base = int.from_bytes(data, "big") << 4
        elif record_type == 4:
            if count != 2 or address:
                raise BuildValidationError(
                    "Intel HEX linear-address record is malformed"
                )
            base = int.from_bytes(data, "big") << 16
        elif record_type in (3, 5):
            if count != 4 or address:
                raise BuildValidationError(
                    "Intel HEX start-address record is malformed"
                )
        else:
            raise BuildValidationError(
                f"Intel HEX record type {record_type} is unsupported"
            )

    if not saw_eof:
        raise BuildValidationError("Intel HEX has no EOF record")
    return memory


def validate_build(map_path: Path, ihx_path: Path, rom_path: Path) -> BuildReport:
    """Validate linker areas, emitted addresses, and final ROM identity."""
    symbols, _ = _parse_map(Path(map_path))

    if symbols["l__HEADER0"] != 4 or symbols["l__HEADER1"] != 14:
        raise BuildValidationError("reset or interrupt vector extent changed")

    if symbols["s__CODE"] != CODE_START:
        raise BuildValidationError(
            f"_CODE starts at 0x{symbols['s__CODE']:04X}, expected 0x{CODE_START:04X}"
        )
    if not symbols["l__CODE"]:
        raise BuildValidationError("_CODE executable area is empty")
    if not symbols["l__GSFINAL"]:
        raise BuildValidationError("_GSFINAL startup area is empty")
    for area in ("_CODE", "_GSINIT", "_GSFINAL"):
        if _area_end(symbols, area) > ROM_SIZE:
            raise BuildValidationError(f"{area} extends outside ROM")
    if _area_end(symbols, "_CODE") != symbols["s__GSINIT"]:
        raise BuildValidationError("_CODE and _GSINIT are not contiguous")
    if _area_end(symbols, "_GSINIT") != symbols["s__GSFINAL"]:
        raise BuildValidationError("_GSINIT and _GSFINAL are not contiguous")

    if symbols["s__DATA"] != DATA_START:
        raise BuildValidationError(
            f"_DATA starts at 0x{symbols['s__DATA']:04X}, "
            f"expected 0x{DATA_START:04X}"
        )
    data_end = _area_end(symbols, "_DATA")
    if data_end > FILESYSTEM_START:
        raise BuildValidationError(
            f"_DATA ends at 0x{data_end:04X} and overlaps the filesystem"
        )
    if symbols["l__INITIALIZED"] or symbols["l__INITIALIZER"]:
        raise BuildValidationError(
            "initialized data is unsupported by the custom CRT"
        )

    memory = _parse_ihex(Path(ihx_path))
    outside = sorted(address for address in memory if address >= ROM_SIZE)
    if outside:
        raise BuildValidationError(
            f"Intel HEX byte at 0x{outside[0]:04X} is outside ROM"
        )

    required_vectors = ((0x0000, 4, "reset vector"), (0x0038, 14, "interrupt vector"))
    for start, size, label in required_vectors:
        missing = next(
            (address for address in range(start, start + size) if address not in memory),
            None,
        )
        if missing is not None:
            raise BuildValidationError(f"{label} is missing byte 0x{missing:04X}")
    if bytes(memory[address] for address in range(4)) != b"\xF3\xC3\x48\x00":
        raise BuildValidationError("reset vector does not disable IRQs and jump to _CODE")

    for area in ("_CODE", "_GSINIT", "_GSFINAL"):
        start = symbols["s_" + area]
        end = _area_end(symbols, area)
        missing = next(
            (address for address in range(start, end) if address not in memory),
            None,
        )
        if missing is not None:
            raise BuildValidationError(
                f"{area} is missing linked byte 0x{missing:04X}"
            )

    claimed_ranges = [(0x0000, 0x0004), (0x0038, 0x0046)]
    claimed_ranges.extend(
        (symbols["s_" + area], _area_end(symbols, area))
        for area in ("_CODE", "_GSINIT", "_GSFINAL")
    )
    unclaimed = next(
        (
            address for address in sorted(memory)
            if not any(start <= address < end for start, end in claimed_ranges)
        ),
        None,
    )
    if unclaimed is not None:
        raise BuildValidationError(
            f"Intel HEX byte at 0x{unclaimed:04X} is unclaimed by the linker map"
        )

    rom = Path(rom_path).read_bytes()
    if len(rom) != ROM_SIZE:
        raise BuildValidationError(
            f"ROM is {len(rom)} bytes, expected {ROM_SIZE}"
        )
    expected = bytearray(b"\xFF" * ROM_SIZE)
    for address, value in memory.items():
        expected[address] = value
    if rom != expected:
        mismatch = next(
            index for index, (actual, wanted) in enumerate(zip(rom, expected))
            if actual != wanted
        )
        raise BuildValidationError(
            f"ROM does not match Intel HEX at 0x{mismatch:04X}"
        )

    rom_used = max(memory, default=-1) + 1
    return BuildReport(rom_size=len(rom), rom_used=rom_used, data_end=data_end)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", required=True, type=Path, dest="map_path")
    parser.add_argument("--ihx", required=True, type=Path, dest="ihx_path")
    parser.add_argument("--rom", required=True, type=Path, dest="rom_path")
    args = parser.parse_args(argv)

    try:
        report = validate_build(args.map_path, args.ihx_path, args.rom_path)
    except (BuildValidationError, OSError) as error:
        print(f"build validation failed: {error}", file=sys.stderr)
        return 1

    percent = report.rom_used * 100 // report.rom_size
    data_used = report.data_end - DATA_START
    data_capacity = FILESYSTEM_START - DATA_START
    print(
        f"ROM verified: {report.rom_used}/{report.rom_size} bytes ({percent}%), "
        f"RAM data: {data_used}/{data_capacity} bytes"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

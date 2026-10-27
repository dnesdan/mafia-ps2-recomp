"""Checked ELF32/MIPS metadata reader; does not pretend to be a decompiler.

Symbol extents are evidence, not a complete control-flow graph. Instruction
surveys only cover sized executable functions, because retail PS2 linkers can
put text and data in the same WAX section. Untyped entry points are reported
separately and remain the upstream analyzer's responsibility.
"""

from __future__ import annotations

from collections import Counter
from dataclasses import asdict, dataclass
import hashlib
from pathlib import Path
import re
import struct

from .common import FormatError, checked_slice

EE_RAM_SIZE = 32 * 1024 * 1024


@dataclass(frozen=True)
class Segment:
    type: int
    offset: int
    vaddr: int
    paddr: int
    file_size: int
    memory_size: int
    flags: int
    alignment: int


@dataclass(frozen=True)
class Section:
    name: str
    type: int
    flags: int
    address: int
    offset: int
    size: int
    link: int
    info: int
    alignment: int
    entry_size: int


@dataclass(frozen=True)
class Symbol:
    name: str
    address: int
    size: int
    type: int
    binding: int
    visibility: int
    section: int
    table: str


def physical_address(address: int) -> int:
    if 0x80000000 <= address < 0xC0000000:
        return address & 0x1FFFFFFF
    return address


class ElfImage:
    def __init__(self, path: Path):
        self.path = Path(path)
        if self.path.stat().st_size > 256 * 1024 * 1024:
            raise FormatError("ELF exceeds 256 MiB parser limit")
        self.data = self.path.read_bytes()
        header = checked_slice(self.data, 0, 52, "ELF header")
        if header[:7] != b"\x7fELF\x01\x01\x01":
            raise FormatError("expected ELF32, little-endian, version 1")
        (kind, machine, version, self.entry_point, phoff, shoff, self.flags,
         ehsize, phentsize, phnum, shentsize, shnum, shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", header, 16)
        if kind != 2 or machine != 8 or version != 1 or ehsize != 52:
            raise FormatError("expected an ELF32 MIPS ET_EXEC with a 52-byte header")
        if phnum == 0xFFFF or shstrndx == 0xFFFF or (shoff and shnum == 0):
            raise FormatError("extended ELF header numbering is not supported")
        if not phnum or phentsize != 32:
            raise FormatError("missing or unsupported ELF program header table")
        checked_slice(self.data, phoff, phnum * phentsize, "program header table")
        self.segments = [Segment(*struct.unpack_from("<IIIIIIII", self.data, phoff + i * phentsize))
                         for i in range(phnum)]
        loads = []
        for segment in self.segments:
            if segment.type != 1:
                continue
            checked_slice(self.data, segment.offset, segment.file_size, "PT_LOAD")
            if segment.file_size > segment.memory_size:
                raise FormatError("PT_LOAD file size exceeds memory size")
            start = physical_address(segment.vaddr)
            if start > EE_RAM_SIZE or segment.memory_size > EE_RAM_SIZE - start:
                raise FormatError("PT_LOAD does not fit 32 MiB EE RAM")
            if segment.alignment not in (0, 1):
                if segment.alignment & (segment.alignment - 1):
                    raise FormatError("PT_LOAD alignment is not a power of two")
                if (segment.vaddr - segment.offset) % segment.alignment:
                    raise FormatError("inconsistent PT_LOAD file/virtual alignment")
            if segment.memory_size:
                for other in loads:
                    other_start = physical_address(other.vaddr)
                    if start < other_start + other.memory_size and other_start < start + segment.memory_size:
                        raise FormatError("overlapping PT_LOAD memory ranges")
                loads.append(segment)
        entry = physical_address(self.entry_point)
        if self.entry_point % 4 or not any(s.flags & 1 and physical_address(s.vaddr) <= entry and entry + 4 <= physical_address(s.vaddr) + s.file_size for s in loads):
            raise FormatError("entry point is not aligned, file-backed executable code")
        self.sections: list[Section] = []
        if shnum:
            if shentsize != 40 or not shoff or shstrndx >= shnum:
                raise FormatError("invalid ELF section header table")
            checked_slice(self.data, shoff, shnum * shentsize, "section header table")
            headers = [struct.unpack_from("<IIIIIIIIII", self.data, shoff + i * shentsize) for i in range(shnum)]
            names_header = headers[shstrndx]
            if names_header[1] != 3:
                raise FormatError("section names do not reference a string table")
            names = checked_slice(self.data, names_header[4], names_header[5], "section name table")
            for item in headers:
                name = self._string(names, item[0], "section name")
                section = Section(name, *item[1:])
                if section.type != 8:  # SHT_NOBITS has no file-backed bytes
                    checked_slice(self.data, section.offset, section.size, f"section {name}")
                if section.address + section.size > 0x100000000:
                    raise FormatError("section virtual address wraps")
                self.sections.append(section)
        elif shoff or shstrndx:
            raise FormatError("inconsistent absent section table")
        self.symbols: list[Symbol] = []
        for section in self.sections:
            if section.type not in (2, 11):
                continue
            if section.entry_size != 16 or section.size % 16 or section.link >= len(self.sections):
                raise FormatError("invalid symbol table")
            strings_section = self.sections[section.link]
            if strings_section.type != 3:
                raise FormatError("symbol table does not reference a string table")
            strings = checked_slice(self.data, strings_section.offset, strings_section.size, "symbol strings")
            for offset in range(section.offset, section.offset + section.size, 16):
                name, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", self.data, offset)
                if 0 < shndx < 0xFF00 and shndx >= len(self.sections):
                    raise FormatError("symbol references a missing section")
                self.symbols.append(Symbol(self._string(strings, name, "symbol"), value, size,
                                           info & 15, info >> 4, other & 3, shndx, section.name))
        self.sha256 = hashlib.sha256(self.data).hexdigest()

    @staticmethod
    def _string(table: bytes, offset: int, context: str) -> str:
        if offset >= len(table):
            raise FormatError(f"{context} string offset outside table")
        end = table.find(b"\0", offset)
        if end < 0:
            raise FormatError(f"unterminated {context} string")
        return table[offset:end].decode("utf-8", errors="replace")

    def bytes_at(self, address: int, size: int) -> bytes:
        address = physical_address(address)
        for segment in self.segments:
            start = physical_address(segment.vaddr)
            if segment.type == 1 and start <= address and size <= segment.file_size - (address - start):
                return checked_slice(self.data, segment.offset + address - start, size, "guest code")
        raise FormatError(f"unmapped file-backed address 0x{address:08x} ({size} bytes)")

    def functions(self) -> list[Symbol]:
        return sorted((s for s in self.symbols if s.type == 2 and s.section != 0), key=lambda s: (s.address, s.name))

    def instruction_survey(self) -> dict:
        counts: Counter[str] = Counter()
        direct_targets: Counter[int] = Counter()
        indirect_sites = []
        mmio_candidates = []
        invalid_functions = []
        seen: set[int] = set()
        for symbol in self.functions():
            if not symbol.size or symbol.size % 4 or symbol.address % 4:
                invalid_functions.append({"name": symbol.name, "reason": "missing or unaligned extent"})
                continue
            if symbol.section >= len(self.sections) or not self.sections[symbol.section].flags & 4:
                invalid_functions.append({"name": symbol.name, "reason": "non-executable section"})
                continue
            section = self.sections[symbol.section]
            if symbol.address < section.address or symbol.address + symbol.size > section.address + section.size:
                invalid_functions.append({"name": symbol.name, "reason": "symbol extent escapes its executable section"})
                continue
            try:
                code = self.bytes_at(symbol.address, symbol.size)
            except FormatError as exc:
                invalid_functions.append({"name": symbol.name, "reason": str(exc)})
                continue
            for offset in range(0, len(code), 4):
                pc = symbol.address + offset
                if pc in seen:
                    continue
                seen.add(pc)
                word = struct.unpack_from("<I", code, offset)[0]
                op, func = word >> 26, word & 63
                counts["words"] += 1
                if op in (2, 3):
                    target = ((pc + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
                    direct_targets[target] += 1
                    counts["jal" if op == 3 else "j"] += 1
                elif op == 0 and func in (8, 9):
                    reg = (word >> 21) & 31
                    name = "jalr" if func == 9 else "jr"
                    counts[name] += 1
                    if func == 9 or reg != 31:
                        indirect_sites.append({"pc": pc, "register": reg, "kind": name})
                elif op == 0 and func in (12, 13):
                    counts["syscall" if func == 12 else "break"] += 1
                elif op in (16, 17, 18, 28, 47):
                    counts[{16: "cop0", 17: "cop1_fpu", 18: "cop2_vu", 28: "mmi", 47: "cache"}[op]] += 1
                if op in (20, 21, 22, 23) or (op == 1 and ((word >> 16) & 31) in (2, 3, 18, 19)):
                    counts["branch_likely"] += 1
                if op == 15 and (word & 0xFFFF) in (0x1000, 0x1100, 0x1200, 0x1F80, 0x1FC0, 0x7000):
                    mmio_candidates.append({"pc": pc, "high_half": word & 0xFFFF,
                                            "evidence": "LUI constant only; not proof of hardware access"})
        return {"scope": "sized STT_FUNC extents in executable sections; not a complete decoder/CFG",
                "counts": dict(counts), "direct_targets": [{"address": a, "references": c} for a, c in sorted(direct_targets.items())],
                "indirect_sites": indirect_sites, "hardware_address_candidates": mmio_candidates,
                "invalid_function_extents": invalid_functions}

    def report(self) -> dict:
        funcs = self.functions()
        debug = [s.name for s in self.sections if s.name.startswith((".debug", ".zdebug", ".stab")) or s.name == ".line"]
        strings = sorted({m.group().decode("ascii") for m in re.finditer(rb"[\x20-\x7e]{4,512}", self.data)
                          if re.search(rb"\.(?:irx|img|dta|fql|pss)|rom0:|cdrom0:", m.group(), re.I)})
        return {"schema_version": 1, "file_size": len(self.data), "sha256": self.sha256,
                "format": "ELF32 little-endian MIPS ET_EXEC", "flags": self.flags,
                "entry_point": self.entry_point, "segments": [asdict(s) for s in self.segments],
                "sections": [asdict(s) for s in self.sections], "symbol_count": len(self.symbols),
                "defined_function_symbols": len(funcs), "unique_function_addresses": len({s.address for s in funcs}),
                "sized_function_symbols": sum(s.size > 0 for s in funcs), "debug_sections": debug,
                "entry_symbols": [asdict(s) for s in self.symbols if s.address == self.entry_point],
                "undefined_symbols": [asdict(s) for s in self.symbols if s.section == 0 and s.name],
                "sdk_symbol_candidates": sorted({s.name for s in self.symbols if re.match(r"(?:sce|Sif|Gs|Dma|fio|iop|_?InitThread)", s.name)}),
                "asset_path_candidates": strings,
                "relocation_sections": [{"name": s.name, "type": s.type, "bytes": s.size,
                                          "entries": s.size // s.entry_size if s.entry_size else None}
                                         for s in self.sections if s.type in (4, 9)],
                "instruction_survey": self.instruction_survey()}

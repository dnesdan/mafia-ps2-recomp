"""Small constructed test inputs. No bytes or names copied from game media."""

from __future__ import annotations

from pathlib import Path
import struct

BLOCK = 2048


def dual16(value: int) -> bytes:
    return struct.pack("<H", value) + struct.pack(">H", value)


def dual32(value: int) -> bytes:
    return struct.pack("<I", value) + struct.pack(">I", value)


def record(name: str | bytes, lba: int, length: int, flags: int = 0,
           *, attributes: int = 0) -> bytes:
    identifier = name.encode("ascii") if isinstance(name, str) else name
    size = 33 + len(identifier) + (not len(identifier) % 2)
    if size > 255:
        raise ValueError("test directory identifier is too long")
    result = bytearray(size)
    result[0] = size
    result[1] = attributes
    result[2:10] = dual32(lba)
    result[10:18] = dual32(length)
    result[18:25] = bytes((124, 1, 1, 0, 0, 0, 0))
    result[25] = flags
    result[28:32] = dual16(1)
    result[32] = len(identifier)
    result[33:33 + len(identifier)] = identifier
    return bytes(result)


def pack_records(records: list[bytes]) -> bytes:
    """Pack records without crossing a logical-sector boundary."""
    output = bytearray()
    for item in records:
        remaining = BLOCK - len(output) % BLOCK
        if len(item) > remaining:
            output.extend(bytes(remaining))
        output.extend(item)
    output.extend(bytes((-len(output)) % BLOCK))
    return bytes(output or bytes(BLOCK))


def write_iso(path: Path, entries: list[bytes],
              payloads: dict[int, bytes] | None = None,
              *, root_data: bytes | None = None,
              root_lba: int = 20, sectors: int = 64) -> Path:
    """Create an ISO9660 primary namespace with fixed fixture sector locations."""
    if root_data is None:
        provisional = pack_records([record(b"\0", root_lba, BLOCK, 2),
                                    record(b"\1", root_lba, BLOCK, 2), *entries])
        root_data = pack_records([record(b"\0", root_lba, len(provisional), 2),
                                  record(b"\1", root_lba, len(provisional), 2), *entries])
    image = bytearray(sectors * BLOCK)
    pvd = bytearray(BLOCK)
    pvd[0:7] = b"\x01CD001\x01"
    pvd[8:40] = b"CONSTRUCTED TEST INPUT".ljust(32, b" ")
    pvd[40:72] = b"PUBLIC_FIXTURE".ljust(32, b" ")
    pvd[80:88] = dual32(sectors)
    pvd[120:124] = dual16(1)
    pvd[124:128] = dual16(1)
    pvd[128:132] = dual16(BLOCK)
    pvd[156:190] = record(b"\0", root_lba, len(root_data), 2)
    pvd[881] = 1
    image[16 * BLOCK:17 * BLOCK] = pvd
    image[17 * BLOCK:17 * BLOCK + 7] = b"\xffCD001\x01"
    image[root_lba * BLOCK:root_lba * BLOCK + len(root_data)] = root_data
    for lba, data in (payloads or {}).items():
        if lba * BLOCK + len(data) > len(image):
            raise ValueError("fixture payload exceeds image")
        image[lba * BLOCK:lba * BLOCK + len(data)] = data
    path.write_bytes(image)
    return path


def mutate(path: Path, offset: int, data: bytes) -> None:
    with path.open("r+b") as output:
        output.seek(offset)
        output.write(data)


def write_elf(path: Path, *, code: bytes | None = None, base: int = 0x100000,
              entry: int | None = None, sectioned: bool = True,
              symbols: list[tuple[str, int, int, int, int]] | None = None,
              memory_extra: int = 16, debug: bool = False) -> dict[str, int]:
    """Construct an ELF32 MIPS executable with optional symbols.

    Symbol tuples contain name, guest address, size, ELF type and section index.
    Section 2 is executable code; section 5 is zero-filled BSS. Returned offsets
    support deliberate fixture corruption without relying on parser internals.
    """
    if code is None:
        code = struct.pack("<IIII", 0, 0x03E00008, 0, 0)
    entry = base if entry is None else entry
    text_offset = 0x100
    image = bytearray(text_offset) + code
    image[:16] = b"\x7fELF\x01\x01\x01" + bytes(9)
    program = (1, text_offset, base, base, len(code), len(code) + memory_extra, 7, 16)
    struct.pack_into("<IIIIIIII", image, 52, *program)
    meta = {"text": text_offset, "ph": 52}
    headers: list[tuple[int, ...]] = []
    section_offset = 0
    if sectioned:
        image.extend(bytes((-len(image)) % 4))
        names = ["", ".shstrtab", ".text", ".strtab", ".symtab", ".bss"]
        if debug:
            names.append(".debug_info")
        shstr = bytearray(b"\0")
        name_offsets = {"": 0}
        for name in names[1:]:
            name_offsets[name] = len(shstr)
            shstr.extend(name.encode() + b"\0")
        meta["shstr"] = len(image)
        image.extend(shstr)
        headers = [(0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
                   (name_offsets[".shstrtab"], 3, 0, 0, meta["shstr"], len(shstr), 0, 0, 1, 0),
                   (name_offsets[".text"], 1, 6, base, text_offset, len(code), 0, 0, 4, 0)]
        if symbols is None:
            symbols = [("ENTRY_MARKER", base, 0, 0, 2),
                       ("fixture_function", base, len(code), 2, 2)]
        strings = bytearray(b"\0")
        records = bytearray(16)
        for name, address, size, symbol_type, section in symbols:
            name_offset = len(strings)
            strings.extend(name.encode() + b"\0")
            records.extend(struct.pack("<IIIBBH", name_offset, address, size,
                                       0x10 | symbol_type, 0, section))
        meta["strtab"] = len(image)
        image.extend(strings)
        image.extend(bytes((-len(image)) % 4))
        meta["symtab"] = len(image)
        image.extend(records)
        headers.extend([
            (name_offsets[".strtab"], 3, 0, 0, meta["strtab"], len(strings), 0, 0, 1, 0),
            (name_offsets[".symtab"], 2, 0, 0, meta["symtab"], len(records), 3, 1, 4, 16),
            (name_offsets[".bss"], 8, 3, base + len(code), 0x7FFFFFF0, memory_extra, 0, 0, 4, 0),
        ])
        if debug:
            offset = len(image)
            image.extend(b"test debug bytes")
            headers.append((name_offsets[".debug_info"], 1, 0, 0, offset, 16, 0, 0, 1, 0))
        image.extend(bytes((-len(image)) % 4))
        section_offset = len(image)
        for item in headers:
            image.extend(struct.pack("<IIIIIIIIII", *item))
        meta["sections"] = section_offset
    struct.pack_into("<HHIIIIIHHHHHH", image, 16,
                     2, 8, 1, entry, 52, section_offset, 0x20924000,
                     52, 32, 1, 40 if sectioned else 0, len(headers), 1 if sectioned else 0)
    path.write_bytes(image)
    return meta

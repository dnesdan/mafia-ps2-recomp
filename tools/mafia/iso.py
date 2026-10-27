"""Bounded ISO9660 reader for 2048-byte PS2 DVD images.

Uses the primary volume namespace, retaining names and implementing cdrom0's
case-insensitive/version-insensitive lookups. Raw reads always address the
original image. Multi-extent regular files are supported; interleaving is
rejected explicitly. This is not a UDF, compressed ISO or raw-CD decoder.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
import hashlib
from pathlib import Path
import re
from typing import BinaryIO, Iterator

from .common import FormatError, safe_destination

SECTOR = 2048
MAX_DIRECTORY_BYTES = 64 * 1024 * 1024
MAX_ENTRIES = 1_000_000


def both16(data: bytes, offset: int) -> int:
    a = int.from_bytes(data[offset:offset + 2], "little")
    b = int.from_bytes(data[offset + 2:offset + 4], "big")
    if a != b:
        raise FormatError("inconsistent ISO9660 little/big-endian 16-bit field")
    return a


def both32(data: bytes, offset: int) -> int:
    a = int.from_bytes(data[offset:offset + 4], "little")
    b = int.from_bytes(data[offset + 4:offset + 8], "big")
    if a != b:
        raise FormatError("inconsistent ISO9660 little/big-endian 32-bit field")
    return a


def lookup_key(path: str) -> str:
    path = re.sub(r"^cdrom\d*:", "", path, flags=re.IGNORECASE)
    parts = [re.sub(r";[0-9]+$", "", p) for p in path.replace("\\", "/").strip("/").split("/")]
    if not parts or any(p in ("", ".", "..") or ":" in p or "\x00" in p for p in parts):
        raise FormatError(f"invalid disc lookup path: {path!r}")
    return "/".join(p.casefold() for p in parts)


@dataclass(frozen=True)
class Entry:
    path: str
    iso_name: str
    is_directory: bool
    size: int
    extents: tuple[tuple[int, int], ...]  # (logical block, byte length)

    def report(self) -> dict:
        result = asdict(self)
        result["kind"] = classify(self.path, self.is_directory)
        return result


def classify(path: str, is_directory: bool = False) -> str:
    if is_directory:
        return "directory"
    suffix = Path(path).suffix.casefold()
    if suffix == ".irx":
        return "iop_module"
    if re.fullmatch(r"S[LCR][A-Z]{2}_[0-9]{3}\.[0-9]{2}", Path(path).name, re.I) or suffix == ".elf":
        return "executable_candidate"
    if suffix in (".pss", ".mpg", ".mpeg", ".wav", ".vag", ".ads"):
        return "media_candidate"
    if suffix in (".dta", ".dat", ".img", ".pak", ".arc", ".bin", ".fql"):
        return "archive_or_overlay_candidate"
    return "file"


class IsoImage:
    def __init__(self, path: Path):
        self.path = Path(path)
        self.stream: BinaryIO = self.path.open("rb")
        try:
            self.size = self.path.stat().st_size
            if self.size < 18 * SECTOR or self.size % SECTOR:
                raise FormatError("expected a complete 2048-byte-sector ISO image")
            pvd = None
            terminated = False
            for sector in range(16, min(self.size // SECTOR, 16 + 256)):
                block = self.read_at(sector * SECTOR, SECTOR)
                if block[1:6] != b"CD001" or block[6] != 1:
                    raise FormatError(f"invalid ISO9660 descriptor at sector {sector}")
                if block[0] == 1 and pvd is None:
                    pvd = block
                if block[0] == 255:
                    terminated = True
                    break
            if pvd is None or not terminated:
                raise FormatError("missing primary volume descriptor or descriptor terminator")
            if both16(pvd, 128) != SECTOR:
                raise FormatError("only 2048-byte logical sectors are supported")
            self.volume_sectors = both32(pvd, 80)
            if self.volume_sectors * SECTOR > self.size:
                raise FormatError("declared ISO volume exceeds file size")
            self.volume_id = pvd[40:72].decode("ascii", errors="strict").rstrip(" \x00")
            root = self._record(pvd[156:156 + pvd[156]], "")
            if root is None or not root.is_directory:
                raise FormatError("invalid root directory record")
            self.entries: list[Entry] = []
            self.by_key: dict[str, Entry] = {}
            self._walk(root)
        except BaseException:
            self.stream.close()
            raise

    def __enter__(self) -> "IsoImage":
        return self

    def __exit__(self, *args: object) -> None:
        self.stream.close()

    def read_at(self, offset: int, count: int) -> bytes:
        if offset < 0 or count < 0 or offset > self.size or count > self.size - offset:
            raise FormatError(f"disc read outside image: offset={offset}, bytes={count}")
        self.stream.seek(offset)
        data = self.stream.read(count)
        if len(data) != count:
            raise FormatError("short disc read")
        return data

    def read_sectors(self, lba: int, count: int) -> bytes:
        return self.read_at(lba * SECTOR, count * SECTOR)

    def _record(self, data: bytes, parent: str) -> Entry | None:
        if len(data) < 34 or data[0] != len(data) or 33 + data[32] > len(data):
            raise FormatError("truncated directory record")
        raw_name = data[33:33 + data[32]]
        is_directory = bool(data[25] & 2)
        if raw_name in (b"\0", b"\1"):
            if parent or not is_directory:
                return None
            name = ""
        else:
            try:
                name = raw_name.decode("ascii")
            except UnicodeDecodeError as exc:
                raise FormatError("non-ASCII primary ISO name") from exc
            lookup_key(name)  # validates separators/traversal below as well
            if any(c in name for c in ("/", "\\")):
                raise FormatError("directory identifier contains a separator")
        if data[26] or data[27]:
            raise FormatError("interleaved ISO9660 files are unsupported")
        if both16(data, 28) != 1:
            raise FormatError("multi-volume ISO9660 is unsupported")
        lba = both32(data, 2) + data[1]  # skip extended-attribute blocks
        size = both32(data, 10)
        if lba * SECTOR > self.volume_sectors * SECTOR or size > self.volume_sectors * SECTOR - lba * SECTOR:
            raise FormatError("directory/file extent exceeds declared volume")
        clean_name = re.sub(r";[0-9]+$", "", name)
        path = f"{parent}/{clean_name}" if parent else clean_name
        return Entry(path, name, is_directory, size, ((lba, size),))

    def _walk(self, root: Entry) -> None:
        pending = [root]
        visited: set[tuple[tuple[int, int], ...]] = set()
        while pending:
            directory = pending.pop()
            if directory.extents in visited:
                raise FormatError("cyclic or aliased directory extent")
            visited.add(directory.extents)
            if directory.size > MAX_DIRECTORY_BYTES:
                raise FormatError("directory exceeds parser allocation limit")
            data = b"".join(self.read_at(lba * SECTOR, size) for lba, size in directory.extents)
            offset = 0
            multi: Entry | None = None
            while offset < len(data):
                length = data[offset]
                if not length:
                    offset = ((offset // SECTOR) + 1) * SECTOR
                    continue
                if length > SECTOR - offset % SECTOR or length > len(data) - offset:
                    raise FormatError("directory record crosses sector or extent boundary")
                record = data[offset:offset + length]
                offset += length
                if len(record) < 34:
                    raise FormatError("short ISO directory record")
                if record[33:33 + record[32]] in (b"\0", b"\1"):
                    if multi is not None:
                        raise FormatError("interrupted multi-extent file")
                    continue
                entry = self._record(record, directory.path)
                if entry is None:
                    continue
                if multi is not None:
                    if entry.iso_name != multi.iso_name or entry.is_directory or multi.is_directory:
                        raise FormatError("invalid multi-extent sequence")
                    entry = Entry(entry.path, entry.iso_name, False, multi.size + entry.size,
                                  multi.extents + entry.extents)
                if record[25] & 0x80:
                    if entry.is_directory:
                        raise FormatError("multi-extent directories are unsupported")
                    multi = entry
                    continue
                multi = None
                key = lookup_key(entry.path)
                if key in self.by_key:
                    raise FormatError(f"ambiguous case/version-insensitive path: {entry.path}")
                self.by_key[key] = entry
                self.entries.append(entry)
                if len(self.entries) > MAX_ENTRIES:
                    raise FormatError("too many ISO directory records")
                if entry.is_directory:
                    pending.append(entry)
            if multi is not None:
                raise FormatError("unterminated multi-extent file")
        self.entries.sort(key=lambda entry: entry.path)

    def lookup(self, path: str) -> Entry:
        try:
            return self.by_key[lookup_key(path)]
        except KeyError as exc:
            raise FileNotFoundError(f"disc path not found: {path}") from exc

    def chunks(self, entry: Entry) -> Iterator[bytes]:
        if entry.is_directory:
            raise IsADirectoryError(entry.path)
        for lba, size in entry.extents:
            offset = lba * SECTOR
            while size:
                count = min(size, 1024 * 1024)
                yield self.read_at(offset, count)
                offset += count
                size -= count

    def read_file(self, path: str, limit: int = 64 * 1024 * 1024) -> bytes:
        entry = self.lookup(path)
        if entry.size > limit:
            raise FormatError(f"{path}: exceeds {limit}-byte read limit")
        return b"".join(self.chunks(entry))

    def extract(self, entry: Entry, root: Path) -> str:
        target = safe_destination(root, entry.path)
        target.parent.mkdir(parents=True, exist_ok=True)
        digest = hashlib.sha256()
        # Existing user files are never overwritten. Verify byte identity instead.
        if target.exists():
            with target.open("rb") as existing:
                for chunk in self.chunks(entry):
                    if existing.read(len(chunk)) != chunk:
                        raise FileExistsError(f"different file already exists: {target}")
                    digest.update(chunk)
                if existing.read(1):
                    raise FileExistsError(f"different file already exists: {target}")
            return digest.hexdigest()
        created = False
        try:
            with target.open("xb") as stream:
                created = True
                for chunk in self.chunks(entry):
                    stream.write(chunk)
                    digest.update(chunk)
        except BaseException:
            if created:
                target.unlink(missing_ok=True)
            raise
        return digest.hexdigest()


def parse_system_cnf(data: bytes) -> dict:
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError as exc:
        raise FormatError("SYSTEM.CNF is not ASCII") from exc
    fields: dict[str, str] = {}
    for line in text.splitlines():
        if "=" not in line or line.lstrip().startswith(("#", ";")):
            continue
        key, value = line.split("=", 1)
        key = key.strip().upper()
        if key in fields:
            raise FormatError(f"duplicate SYSTEM.CNF field: {key}")
        fields[key] = value.strip()
    boot = fields.get("BOOT2", "")
    if not re.fullmatch(r"cdrom\d*:[\\/].+", boot, re.I):
        raise FormatError("SYSTEM.CNF has no supported BOOT2 disc path")
    lookup_key(boot)
    serial_match = re.search(r"([A-Z]{4})_([0-9]{3})\.([0-9]{2})(?:;[0-9]+)?$", boot, re.I)
    serial = None
    region = "unknown"
    if serial_match:
        prefix, first, last = serial_match.groups()
        prefix = prefix.upper()
        serial = f"{prefix}-{first}{last}"
        region = {"SLUS": "North America", "SCUS": "North America", "SLES": "Europe",
                  "SCES": "Europe", "SLPS": "Japan", "SLPM": "Japan", "SCPS": "Japan"}.get(prefix, "unknown")
    return {"boot2": boot, "serial": serial, "region_from_serial": region, "fields": fields}

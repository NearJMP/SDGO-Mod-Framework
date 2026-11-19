"""Private, session-scoped x64 worker for the native map resource broker."""
from __future__ import annotations

from collections import OrderedDict
import hashlib
import os
from pathlib import Path
import re
import struct
import sys
import zlib

from owner.native_archive import CHUNK_SIZE, MAX_ARCHIVE_BYTES, MAX_RESOURCE_BYTES, OverlayReader, part_paths, update_digest
from . import storage

PIPE_PREFIX = r"\\.\pipe\sdgo-resource-v1-"
REQUEST_BYTES = 244
REPLY = struct.Struct('<4I')
PROTOCOL = 0x31525358
CACHE_BYTES = 1024**3


def is_x64_manager(path):
    try:
        with path.open('rb') as stream:
            dos = stream.read(64)
            if len(dos) != 64 or dos[:2] != b'MZ':
                return False
            pe = struct.unpack_from('<I', dos, 60)[0]
            if not 64 <= pe <= 1024**2:
                return False
            stream.seek(pe)
            header = stream.read(26)
            return (len(header) == 26 and header[:6] == b'PE\0\0\x64\x86'
                    and header[24:26] == b'\x0b\x02')
    except OSError:
        return False


def _digest(paths):
    digest = hashlib.sha256()
    for path in paths:
        with path.open('rb') as stream:
            update_digest(digest, stream)
    return digest.hexdigest()


class Session:
    def __init__(self, root, cache):
        from owner.maps.bundle import allowed_overlay
        folder = storage.safe_child(root, 'sdgo/mods/map_catalog')
        cfg = storage.ini_values((folder / 'resource-overlay.ini').read_bytes())['runtime']
        if (cfg.get('schema'), cfg.get('index'), cfg.get('archive')) != ('2', 'resources.zdx', 'resources.zpk'):
            raise ValueError('unsupported runtime resource package')
        parts = int(cfg.get('parts', '1'))
        if not 1 <= parts <= 32:
            raise ValueError('archive part count outside the native limit')
        chunks = int(cfg.get('part_chunks', '0'))
        index = storage.safe_child(folder, 'resources.zdx')
        paths = [storage.safe_child(folder, part.name) for part in part_paths('resources.zpk', parts)]
        if (index.stat().st_size > 64 * 1024**2
                or sum(path.stat().st_size for path in paths) > MAX_ARCHIVE_BYTES):
            raise ValueError('runtime resource package exceeds its budget')
        index_data = index.read_bytes()
        if (hashlib.sha256(index_data).hexdigest() != cfg.get('index_sha256')
                or _digest(paths) != cfg.get('archive_sha256')):
            raise ValueError('runtime resource package seal differs')
        self.reader = OverlayReader(index_data, paths[0], parts, chunks)
        if any(not allowed_overlay(name) or not 12 <= size <= MAX_RESOURCE_BYTES or
               used != (size + CHUNK_SIZE - 1) // CHUNK_SIZE or
               (parts > 1 and start // chunks != (start + used - 1) // chunks)
               for name, (size, start, used) in self.reader.entries.items()):
            self.reader.close()
            raise ValueError('unsupported map resource')
        self.cache = cache
        self.entries = OrderedDict()
        self.used = 0
        self.serial = 0

    def close(self):
        self.reader.close()

    def get(self, name):
        cached = self.entries.get(name)
        if cached is not None:
            self.entries.move_to_end(name)
            return 2, *cached[:2]
        stored, start, chunks = self.reader.entries[name]
        packed = self.reader._archive.read(start, chunks)[:stored]
        if packed[:8] != b'ZOAGZIP1' or len(packed) != stored or len(packed) < 12:
            raise ValueError('invalid compressed map entry')
        size = struct.unpack_from('<I', packed, 8)[0]
        if not 8 <= size <= MAX_RESOURCE_BYTES:
            raise ValueError('decoded size outside the native limit')
        decoder = zlib.decompressobj(31)
        raw = decoder.decompress(packed[12:], size + 1)
        if (len(raw) != size or not decoder.eof or decoder.unused_data or
                decoder.unconsumed_tail or raw.startswith(b'ZOAGZIP1')):
            raise ValueError('invalid decoded map entry')
        disk_bytes = ((size + CHUNK_SIZE - 1) // CHUNK_SIZE) * (CHUNK_SIZE + 8)
        for key, (old_id, _, old_bytes) in list(self.entries.items()):
            if self.used + disk_bytes <= CACHE_BYTES:
                break
            try:
                self.path(old_id).unlink(missing_ok=True)
            except PermissionError:
                continue  # Native FILE* still holds this stream.
            self.used -= old_bytes
            del self.entries[key]
        if self.used + disk_bytes > CACHE_BYTES:
            raise ValueError('runtime resource cache is full')
        self.serial += 1
        path = self.path(self.serial)
        try:
            with path.open('xb', buffering=1024**2) as output:
                for index, start in enumerate(range(0, size, CHUNK_SIZE)):
                    output.write(struct.pack('<2I', index + 1, max(0, index - 1)))
                    output.write(raw[start:start + CHUNK_SIZE].ljust(CHUNK_SIZE, b'\0'))
        except BaseException:
            path.unlink(missing_ok=True)
            raise
        self.used += disk_bytes
        self.entries[name] = self.serial, size, disk_bytes
        return 1, self.serial, size

    def path(self, serial):
        return self.cache / f'{serial:08x}.zpk'


def _read(stream):
    data = bytearray()
    while len(data) < REQUEST_BYTES:
        block = stream.read(REQUEST_BYTES - len(data))
        if not block:
            return None
        data.extend(block)
    return bytes(data)


def run(pipe, session_id):
    if (os.name != 'nt' or struct.calcsize('P') != 8 or not getattr(sys, 'frozen', False)
            or not re.fullmatch('[0-9a-f]{32}', session_id) or pipe != PIPE_PREFIX + session_id):
        return 2
    root = Path(sys.executable).resolve().parent
    cache = storage.safe_child(root, 'sdgo/local-state/resource-worker/' + session_id)
    with open(pipe, 'r+b', buffering=0) as channel:
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.mkdir()
        session = None
        try:
            session = Session(root, cache)
            channel.write(REPLY.pack(PROTOCOL, 1, 0, 64))
            while (request := _read(channel)) is not None:
                serial, name = struct.unpack('<I240s', request)
                try:
                    status, file_id, size = session.get(name.rstrip(b'\0').decode('ascii'))
                except (OSError, ValueError, KeyError, zlib.error):
                    status, file_id, size = 0, 0, 0
                channel.write(REPLY.pack(serial, status, file_id, size))
        except OSError:
            return 1
        finally:
            if session is not None:
                session.close()
            # The game owns all native FILE* instances and closes them at exit.
            for path in cache.glob('*.zpk'):
                try:
                    path.unlink()
                except OSError:
                    pass
            try:
                cache.rmdir()
            except OSError:
                pass
    return 0

import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from manager import dependencies
from manager.resource_worker import Session
from owner.maps import bundle, deployment
from owner.maps.compiler import CompiledMap


class LiveResourceWorkerTests(unittest.TestCase):
    def test_large_publication_requires_worker_and_produces_cached_native_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            client = root / 'client'
            sdgo = client / 'sdgo'
            maps = sdgo / 'mods/map_catalog'
            worker = sdgo / 'mods/resource_worker'
            maps.mkdir(parents=True)
            worker.mkdir()
            (client / 'GOnline.exe').write_bytes(b'MZ')
            dos = bytearray(64)
            dos[:2] = b'MZ'
            struct.pack_into('<I', dos, 60, 64)
            (client / 'SDGO Mod Manager.exe').write_bytes(dos + b'PE\0\0\x64\x86' + b'\0' * 18 + b'\x0b\x02')
            (maps / 'mod.ini').write_text('[mod]\nid=map_catalog\nversion=0.8.0\n')
            (worker / 'mod.ini').write_text('[mod]\nid=resource_worker\nversion=1.0.0\n')
            (worker / 'manager.json').write_text(json.dumps({
                'id': 'resource_worker', 'provides': {'live_map_resources': 1},
            }))
            (worker / 'resource_worker.dll').write_bytes(b'MZ')
            path = 'txrs/m143.txr'
            raw = b'valid resource bytes to reconstruct' * 32
            build = CompiledMap(map_no=143, map_id=500, variant='pvp', prefix='m143')
            build.report = {'map_key': 'worker-test', 'trn': 'Trns\\m143.trn'}
            build.resources = {'trns/m143.trn': b'terrain data', path: raw}
            with patch.object(bundle, 'LIVE_WORKER_THRESHOLD', 520), patch.object(bundle, 'PART_CHUNKS', 1):
                publication = bundle.build_publication([build], root / 'publication')
            self.assertEqual(publication['requires_runtime'], bundle.LIVE_WORKER_REQUIREMENT)
            source = Path(publication['client_zip'])
            (sdgo / 'sdgo.ini').write_text('[mods]\nresource_worker=0\n')
            with self.assertRaisesRegex(ValueError, 'resource_worker'):
                deployment.install_package(client, source)
            self.assertFalse((maps / 'catalog.bin').exists())
            (sdgo / 'sdgo.ini').write_text('[mods]\nresource_worker=1\n')
            capsule = sdgo / 'mods/capsule_catalog'
            capsule.mkdir()
            (capsule / 'mod.ini').write_text('[mod]\nid=capsule_catalog\nversion=1.3.0\n')
            with self.assertRaisesRegex(ValueError, 'capsule_catalog'):
                deployment.install_package(client, source)
            (capsule / 'manager.json').write_text(json.dumps({
                'id': 'capsule_catalog', 'provides': {'live_map_broker': 1},
            }))
            deployment.install_package(client, source)
            with self.assertRaisesRegex(ValueError, 'resource_worker'):
                dependencies.enforce_install(client, {'sdgo/sdgo.ini': b'[mods]\nresource_worker=0\n'}, 'client')

            cache = root / 'cache'
            cache.mkdir()
            session = Session(client, cache)
            try:
                status, file_id, size = session.get(path)
                self.assertEqual((status, size), (1, len(raw)))
                self.assertEqual(session.get(path), (2, file_id, len(raw)))
                chunk_file = session.path(file_id).read_bytes()
                self.assertEqual(struct.unpack_from('<II', chunk_file), (1, 0))
                rebuilt = b''.join(chunk_file[i + 8:i + 520] for i in range(0, len(chunk_file), 520))
                self.assertEqual(rebuilt[:size], raw)
            finally:
                session.close()
            with (maps / 'resources.1.zpk').open('ab') as stream:
                stream.write(b'changed')
            with self.assertRaisesRegex(ValueError, 'seal differs'):
                Session(client, cache)

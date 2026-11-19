import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from manager import client_memory
from owner.maps import bundle, deployment
from owner.maps.compiler import CompiledMap
from owner.native_archive import OverlayReader


class ClientMemoryTests(unittest.TestCase):
    def test_flag_is_reversible_and_every_other_byte_is_pinned(self):
        raw = bytearray(512)
        raw[:2] = b'MZ'
        struct.pack_into('<I', raw, 60, 64)
        raw[64:68] = b'PE\0\0'
        struct.pack_into('<H14xHHH', raw, 68, 0x14c, 224, 0x10f, 0x10b)
        original = bytes(raw)
        pins = {hashlib.sha256(original).hexdigest()}
        with patch.object(client_memory, 'SUPPORTED_ORIGINALS', pins):
            enabled = client_memory.image_variant(original, True)
            self.assertEqual([i for i, n in enumerate(original) if n != enabled[i]], [86])
            self.assertEqual(client_memory.image_variant(enabled, True), enabled)
            self.assertEqual(client_memory.image_variant(enabled, False), original)
            with self.assertRaises(ValueError):
                client_memory.image_variant(enabled[:-1] + b'\x01', True)


class MapCapacityTests(unittest.TestCase):
    def test_file_import_serves_later_part_and_removes_it_on_small_reinstall(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            client = root / 'client'
            mod = client / 'sdgo/mods/map_catalog'
            mod.mkdir(parents=True)
            (client / 'GOnline.exe').write_bytes(b'MZ')
            (mod / 'mod.ini').write_bytes(b'[mod]\nid=map_catalog\n')
            build = CompiledMap(map_no=143, map_id=500, variant='pvp', prefix='m143')
            build.report = {'map_key': 'capacity-test', 'trn': 'Trns\\m143.trn'}
            build.resources = {'trns/m143.trn': b'first', 'txrs/m143.txr': b'second'}
            with patch.object(bundle, 'PART_CHUNKS', 1):
                package = Path(bundle.build_publication([build], root / 'large')['client_zip'])
            receipt = deployment.install_package(client, package)['receipt']
            self.assertEqual(receipt['resources']['archive_parts'], 2)
            with OverlayReader((mod / 'resources.zdx').read_bytes(), mod / 'resources.zpk', 2, 1) as reader:
                self.assertEqual(reader.read('txrs/m143.txr'), b'second')
            (mod / 'resources.1.zpk').write_bytes(b'prior part')
            package = Path(bundle.build_publication([build], root / 'small')['client_zip'])
            renamed = deployment.os.replace

            def fail_receipt(source, target):
                if source.name == 'receipt.json':
                    raise OSError('test commit failure')
                return renamed(source, target)

            with patch.object(deployment.os, 'replace', side_effect=fail_receipt):
                with self.assertRaises(OSError):
                    deployment.install_package(client, package)
            self.assertEqual((mod / 'resources.1.zpk').read_bytes(), b'prior part')
            self.assertEqual(deployment.install_package(client, package)['removed'],
                             ['sdgo/mods/map_catalog/resources.1.zpk'])
            self.assertFalse((mod / 'resources.1.zpk').exists())

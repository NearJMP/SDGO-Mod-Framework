import base64
import io
from importlib import metadata
import random
import struct
import unittest
from unittest import mock
import zlib

from PIL import Image, PngImagePlugin

from owner.stickers import images

HEIF_RGBA = base64.b64decode(
    "AAAAGGZ0eXBoZWljAAAAAG1pZjFoZWljAAACa21ldGEAAAAAAAAAIWhkbHIAAAAAAAAAAHBpY3QAAAAAAAAAAAAAAAAAAAAANGlsb2MAAAAAREAAAgABAAAAAAKLAAEAAAAAAAAAcwACAAAAAAL+AAEAAAAAAAAAQwAAADhpaW5mAAAAAAACAAAAFWluZmUCAAAAAAEAAGh2YzEAAAAAFWluZmUCAAAAAAIAAGh2YzEAAAAADnBpdG0AAAAAAAEAAAGqaXBycAAAAYJpcGNvAAAAdWh2Y0MBA3AAAACQAAAAAAAe8AD8/fj4AAAPA2AAAQAYQAEMAf//A3AAAAMAkAAAAwAAAwAeugJAYQABAClCAQEDcAAAAwCQAAADAAADAB6gIIEFluqumubgIaDAgAAADIAAAAMAhGIAAQAGRAHBc8GJAAAAE2NvbHJuY2x4AAEADQAGgAAAABRpc3BlAAAAAAAAAEAAAABAAAAAKGNsYXAAAAAHAAAAAQAAAAUAAAAB////xwAAAAL////FAAAAAgAAABBwaXhpAAAAAAMICAgAAABxaHZjQwEECAAAAJ/4AAAAAB7wAPz8+PgAAA8DYAABABdAAQwB//8ECAAAAwCf+AAAAwAAHroCQGEAAQAmQgEBBAgAAAMAn/gAAAMAAB7AggQWW6q6a5sCAAADADIAAAMAAhBiAAEABkQBwXPBiQAAAA5waXhpAAAAAAEIAAAAJ2F1eEMAAAAAdXJuOm1wZWc6aGV2YzoyMDE1OmF1eGlkOjEAAAAAIGlwbWEAAAAAAAAAAgABBYECAwWEAAIFhgMHiIQAAAAaaXJlZgAAAAAAAAAOYXV4bAACAAEAAQAAAL5tZGF0AAAAbygBrwayHoBw0PFziJFtndv/RcOq3z8DACkHgYHjHvZ6v/ou4x3rWuIRDds7Wo847o9//+c1eh2NFpDlJ9tJ3/4qfY/UUTls5PjKrh+4slovnwbK2/hH/H6/QFe0O7wuwiKAU05FdzA60Du1UP3TeQAAAD8oAa4NZDUCCXyIBS2ybiTCN/rZOFc3gNNgSOLDvn+cUKO3mgqxWdwywn+yvmH+nz6Qg24WU5SHWLs1QGOd8W4="
)


def encoded(fmt, *, size=(16, 12), alpha=120):
    mode = "RGB" if fmt == "JPEG" else "RGBA"
    color = (20, 90, 160) if mode == "RGB" else (20, 90, 160, alpha)
    picture = Image.new(mode, size, color)
    output = io.BytesIO()
    options = {}
    if fmt == "WEBP":
        options["lossless"] = True
    if fmt == "ICO":
        picture = Image.new("RGBA", (16, 16), (20, 90, 160, alpha))
        options["sizes"] = [(16, 16)]
    picture.save(output, fmt, **options)
    return output.getvalue()


def oversized_png_header(width, height):
    raw = bytearray(encoded("PNG", size=(1, 1)))
    raw[16:24] = struct.pack(">II", width, height)
    raw[29:33] = struct.pack(">I", zlib.crc32(raw[12:29]) & 0xFFFFFFFF)
    return bytes(raw)


class StickerImageTests(unittest.TestCase):
    def test_declared_common_raster_formats_decode_and_canonicalize(self):
        cases = {
            "PNG": "sample.png",
            "JPEG": "sample.jpeg",
            "BMP": "sample.bmp",
            "GIF": "sample.gif",
            "WEBP": "sample.webp",
            "TIFF": "sample.tiff",
            "TGA": "sample.tga",
            "DDS": "sample.dds",
            "ICO": "sample.ico",
        }
        capabilities = images.codec_capabilities()
        self.assertEqual("Pillow>=11.1,<13", capabilities["dependency"])
        for fmt, name in cases.items():
            with self.subTest(fmt=fmt):
                self.assertTrue(capabilities["formats"][fmt.lower()]["available"])
                result = images.canonicalize_raster(encoded(fmt), source_name=name)
                self.assertEqual(fmt, result["source"]["format"])
                self.assertEqual(1, result["source"]["frame_count"])
                with Image.open(io.BytesIO(result["png"])) as canonical:
                    canonical.load()
                    self.assertEqual("PNG", canonical.format)
                    self.assertEqual("RGBA", canonical.mode)
                    self.assertEqual(
                        (result["width"], result["height"]), canonical.size
                    )

    def test_transparency_and_opaque_white_pixels_survive_supporting_formats(self):
        cases = {
            "PNG": "png",
            "GIF": "gif",
            "WEBP": "webp",
            "TIFF": "tiff",
            "TGA": "tga",
            "DDS": "dds",
            "ICO": "ico",
        }
        for fmt, extension in cases.items():
            with self.subTest(fmt=fmt):
                picture = Image.new("RGBA", (16, 16), (20, 90, 160, 255))
                picture.putpixel((0, 0), (255, 255, 255, 0))
                picture.putpixel((8, 8), (255, 255, 255, 255))
                output = io.BytesIO()
                options = {"lossless": True} if fmt == "WEBP" else {}
                if fmt == "ICO":
                    options["sizes"] = [(16, 16)]
                picture.save(output, fmt, **options)
                result = images.canonicalize_raster(
                    output.getvalue(), source_name=f"sample.{extension}"
                )
                with Image.open(io.BytesIO(result["png"])) as canonical:
                    self.assertEqual(0, canonical.getpixel((0, 0))[3])
                    self.assertEqual((255, 255, 255, 255), canonical.getpixel((8, 8)))

    def test_avif_is_enabled_only_when_the_actual_codec_round_trips(self):
        capability = images.codec_capabilities()["formats"]["avif"]
        if not (capability["available"] and capability["encoder"]):
            self.skipTest("This Pillow build has no tested AVIF decoder/encoder")
        result = images.canonicalize_raster(encoded("AVIF"), source_name="sample.avif")
        self.assertEqual("AVIF", result["source"]["format"])
        self.assertEqual((16, 12), (result["width"], result["height"]))

    def test_animated_gif_requires_explicit_first_frame_policy(self):
        first = Image.new("RGBA", (12, 8), (255, 0, 0, 255))
        second = Image.new("RGBA", (12, 8), (0, 0, 255, 255))
        output = io.BytesIO()
        first.save(
            output,
            "GIF",
            save_all=True,
            append_images=[second],
            duration=[20, 20],
            loop=0,
        )
        raw = output.getvalue()
        with self.assertRaisesRegex(ValueError, "allow_gif_first_frame"):
            images.inspect_raster(raw, source_name="animated.gif")
        result = images.canonicalize_raster(
            raw,
            source_name="animated.gif",
            allow_gif_first_frame=True,
        )
        self.assertEqual(2, result["source"]["frame_count"])
        self.assertEqual("first-frame", result["source"]["frame_policy"])
        self.assertEqual(1, result["source"]["discarded_frames"])
        with Image.open(io.BytesIO(result["png"])) as canonical:
            self.assertEqual((255, 0, 0, 255), canonical.getpixel((3, 3)))

    def test_animated_webp_requires_shared_explicit_first_frame_policy(self):
        first = Image.new("RGBA", (12, 8), (255, 255, 255, 96))
        first.putpixel((3, 3), (255, 255, 255, 255))
        second = Image.new("RGBA", (12, 8), (0, 0, 255, 255))
        output = io.BytesIO()
        try:
            first.save(
                output,
                "WEBP",
                save_all=True,
                append_images=[second],
                duration=[20, 20],
                loop=0,
                lossless=True,
            )
        except OSError:
            self.skipTest("This Pillow build has no animated WebP encoder")
        raw = output.getvalue()
        with self.assertRaisesRegex(ValueError, "allow_animated_first_frame"):
            images.inspect_raster(raw, source_name="animated.webp")
        result = images.canonicalize_raster(
            raw,
            source_name="animated.webp",
            allow_animated_first_frame=True,
        )
        self.assertEqual(2, result["source"]["frame_count"])
        self.assertEqual("first-frame", result["source"]["frame_policy"])
        self.assertEqual(1, result["source"]["discarded_frames"])
        with Image.open(io.BytesIO(result["png"])) as canonical:
            self.assertEqual((255, 255, 255, 255), canonical.getpixel((3, 3)))
            self.assertEqual(96, canonical.getpixel((0, 0))[3])

    def test_contain_sizing_preserves_aspect_alpha_and_reports_padding(self):
        source = Image.new("RGBA", (20, 10), (220, 240, 250, 128))
        source.putpixel((10, 5), (255, 255, 255, 255))
        stream = io.BytesIO()
        source.save(stream, "PNG")
        result = images.canonicalize_raster(
            stream.getvalue(),
            source_name="apron.png",
            target_size=(32, 32),
        )
        self.assertEqual({"width": 32, "height": 16}, result["sizing"]["rendered_size"])
        self.assertEqual({"x": 0, "y": 8}, result["sizing"]["offset"])
        self.assertEqual(
            {"left": 0, "top": 8, "right": 0, "bottom": 8},
            result["sizing"]["padding"],
        )
        self.assertEqual([0, 0, 0, 0], result["sizing"]["padding_rgba"])
        with Image.open(io.BytesIO(result["png"])) as canonical:
            self.assertEqual((0, 0, 0, 0), canonical.getpixel((0, 0)))
            self.assertGreater(canonical.getpixel((16, 16))[3], 0)
        repeat = images.canonicalize_raster(
            stream.getvalue(),
            source_name="apron.png",
            target_size=(32, 32),
        )
        self.assertEqual(result["png"], repeat["png"])
        self.assertEqual(result["sha256"], repeat["sha256"])

    def test_source_name_is_a_filename_and_must_match_detected_bytes(self):
        raw = encoded("PNG")
        with self.assertRaisesRegex(ValueError, "not a path or URL"):
            images.inspect_raster(raw, source_name="C:/private/sample.png")
        with self.assertRaisesRegex(ValueError, "does not match"):
            images.inspect_raster(raw, source_name="sample.jpg")
        inspection = images.inspect_raster(raw, source_name="大肥鱼.png")
        self.assertEqual("PNG", inspection["format"])

    def test_heif_decoder_is_decoder_only_and_preserves_alpha(self):
        capability = images.codec_capabilities()["formats"]["heic"]
        self.assertTrue(capability["available"])
        self.assertTrue(capability["decoder"])
        self.assertFalse(capability["encoder"])
        self.assertEqual("1.23.4", capability["libheif"])
        wheel_files = {
            str(item).replace("\\", "/").casefold()
            for item in metadata.distribution("pi-heif-decoder").files or []
        }
        self.assertTrue(any(name.startswith("_pi_heif.") for name in wheel_files))
        self.assertTrue(any(name.startswith("libheif-") for name in wheel_files))
        self.assertTrue(any(name.startswith("libde265-") for name in wheel_files))
        self.assertFalse(
            any("x265" in name or "pillow_heif" in name for name in wheel_files)
        )
        result = images.canonicalize_raster(HEIF_RGBA, source_name="sample.heic")
        self.assertEqual("HEIF", result["source"]["format"])
        self.assertEqual((7, 5), (result["width"], result["height"]))
        self.assertEqual([0, 128], result["source"]["alpha_range"])
        with Image.open(io.BytesIO(result["png"])) as canonical:
            self.assertEqual("RGBA", canonical.mode)
            self.assertEqual((10, 20, 30, 128), canonical.getpixel((0, 0)))
            self.assertEqual((0, 0, 0, 0), canonical.getpixel((3, 2)))
        repeat = images.canonicalize_raster(HEIF_RGBA, source_name="sample.heif")
        self.assertEqual(result["png"], repeat["png"])

    def test_heif_security_and_frame_limits_are_checked_before_pixel_decode(self):
        import pi_heif

        original = pi_heif.options.DISABLE_SECURITY_LIMITS
        try:
            pi_heif.options.DISABLE_SECURITY_LIMITS = True
            with self.assertRaisesRegex(ValueError, "security limits"):
                images.inspect_raster(HEIF_RGBA, source_name="sample.heic")
        finally:
            pi_heif.options.DISABLE_SECURITY_LIMITS = original

        class Frame:
            def __init__(self, size):
                self.size = size
                self.loaded = False

            def load(self):
                self.loaded = True

        class File:
            primary_index = 0

            def __init__(self, frames):
                self.frames = frames

            def __len__(self):
                return len(self.frames)

            def __getitem__(self, index):
                return self.frames[index]

        class Module:
            def __init__(self, source):
                self.source = source

            def open_heif(self, *_args, **_kwargs):
                return self.source

        oversized = Frame((images.MAX_DIMENSION + 1, 1))
        with mock.patch.object(
            images, "_heif_module", return_value=(Module(File([oversized])), {})
        ):
            with self.assertRaisesRegex(ValueError, "HEIF frame size"):
                images.inspect_raster(HEIF_RGBA, source_name="sample.heic")
        self.assertFalse(oversized.loaded)

        first, second = Frame((2, 2)), Frame((2, 2))
        with mock.patch.object(
            images, "_heif_module", return_value=(Module(File([first, second])), {})
        ):
            with self.assertRaisesRegex(ValueError, "multi-frame HEIF"):
                images.inspect_raster(HEIF_RGBA, source_name="sample.heic")
        self.assertFalse(first.loaded)
        self.assertFalse(second.loaded)

    def test_svg_shape_subset_renders_transparency_and_is_deterministic(self):
        raw = (
            b'<svg xmlns="http://www.w3.org/2000/svg" width="40" height="20" '
            b'viewBox="0 0 40 20"><g opacity="0.75" transform="translate(1 0)">'
            b'<rect x="0" y="0" width="19" height="20" fill="#ff000080"/>'
            b'<path d="M 21 2 L 37 10 L 21 18 Z" fill="blue"/></g></svg>'
        )
        import resvg_py

        original = resvg_py.svg_to_bytes
        with mock.patch("resvg_py.svg_to_bytes", wraps=original) as renderer:
            result = images.canonicalize_raster(raw, source_name="shape.svg")
        kwargs = renderer.call_args.kwargs
        self.assertTrue(kwargs["skip_system_fonts"])
        self.assertIsNone(kwargs["resources_dir"])
        self.assertEqual([], kwargs["font_files"])
        self.assertEqual([], kwargs["font_dirs"])
        self.assertEqual("SVG", result["source"]["format"])
        self.assertEqual((40, 20), (result["width"], result["height"]))
        self.assertTrue(result["has_transparency"])
        with Image.open(io.BytesIO(result["png"])) as canonical:
            self.assertEqual((40, 20), canonical.size)
            self.assertGreater(canonical.getpixel((5, 5))[3], 0)
            self.assertEqual((0, 0, 0, 0), canonical.getpixel((39, 19)))
        repeat = images.canonicalize_raster(raw, source_name="shape.svg")
        self.assertEqual(result["png"], repeat["png"])

    def test_svg_active_resource_and_complex_xml_are_rejected_before_renderer(self):
        cases = {
            "doctype": b'<!DOCTYPE svg [<!ENTITY xxe SYSTEM "file:///secret">]><svg xmlns="http://www.w3.org/2000/svg" width="1" height="1">&xxe;</svg>',
            "processing instruction": b'<?xml version="1.0"?><svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"/>',
            "comment": b'<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><!--hidden--></svg>',
            "image": b'<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><image href="file:///secret"/></svg>',
            "script": b'<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><script>alert(1)</script></svg>',
            "text": b'<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><text>load font</text></svg>',
            "foreign object": b'<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><foreignObject/></svg>',
            "style": b'<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><style>@import url(https://example.invalid/a.css)</style></svg>',
            "paint url": b'<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><rect width="1" height="1" fill="url(#gradient)"/></svg>',
            "namespace": b'<svg xmlns="http://www.w3.org/2000/svg" xmlns:x="urn:bad" width="1" height="1"><rect x:href="file:///secret"/></svg>',
        }
        with mock.patch("resvg_py.svg_to_bytes") as renderer:
            for label, raw in cases.items():
                with self.subTest(label=label):
                    with self.assertRaises(ValueError):
                        images.inspect_raster(raw, source_name="sample.svg")
        renderer.assert_not_called()

    def test_svg_dimension_node_path_and_extension_limits_fail_closed(self):
        too_large = (
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{images.MAX_DIMENSION + 1}" '
            'height="1"/>'.encode()
        )
        with self.assertRaisesRegex(ValueError, "dimensions"):
            images.inspect_raster(too_large, source_name="sample.svg")
        deep = (
            '<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1">'
            + "<g>" * 40
            + '<rect width="1" height="1"/>'
            + "</g>" * 40
            + "</svg>"
        ).encode()
        with self.assertRaisesRegex(ValueError, "nesting depth"):
            images.inspect_raster(deep, source_name="sample.svg")
        path = b'<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><path d="M 1000001 0 L 0 0"/></svg>'
        with self.assertRaisesRegex(ValueError, "out-of-range"):
            images.inspect_raster(path, source_name="sample.svg")
        with self.assertRaisesRegex(ValueError, "does not match"):
            images.inspect_raster(
                b'<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"/>',
                source_name="sample.png",
            )
        with self.assertRaisesRegex(ValueError, "corrupt or unsupported"):
            images.inspect_raster(b"not-heic", source_name="sample.heic")

    def test_file_pixel_frame_and_corruption_limits_fail_closed(self):
        with self.assertRaisesRegex(ValueError, "1.."):
            images.inspect_raster(b"x" * (images.MAX_INPUT_BYTES + 1))
        with self.assertRaisesRegex(ValueError, "pixel"):
            images.inspect_raster(
                oversized_png_header(4097, 4097), source_name="large.png"
            )
        with self.assertRaisesRegex(ValueError, "corrupt or unsupported"):
            images.inspect_raster(b"not an image", source_name="broken.png")

    def test_canonical_validation_accepts_canonical_png_larger_than_import_limit(self):
        side = 2250
        raw = random.Random(7).randbytes(side * side * 3)
        picture = Image.frombytes("RGB", (side, side), raw)
        source = io.BytesIO()
        picture.save(source, "BMP")
        self.assertLess(len(source.getvalue()), images.MAX_INPUT_BYTES)
        canonical = images.canonicalize_raster(
            source.getvalue(), source_name="noise.bmp"
        )["png"]
        self.assertGreater(len(canonical), images.MAX_INPUT_BYTES)
        self.assertLess(len(canonical), images.MAX_CANONICAL_BYTES)
        inspected = images.validate_canonical_png(canonical)
        self.assertEqual((side, side), (inspected["width"], inspected["height"]))

    def test_canonical_validation_rejects_metadata_bearing_png(self):
        picture = Image.new("RGBA", (8, 8), (1, 2, 3, 4))
        metadata = PngImagePlugin.PngInfo()
        metadata.add_text("private-note", "must not be retained")
        output = io.BytesIO()
        picture.save(output, "PNG", pnginfo=metadata)
        with self.assertRaisesRegex(ValueError, "not canonical"):
            images.validate_canonical_png(output.getvalue())
        canonical = images.canonicalize_raster(output.getvalue())["png"]
        images.validate_canonical_png(canonical)


if __name__ == "__main__":
    unittest.main()

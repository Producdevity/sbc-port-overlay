# SPDX-License-Identifier: MIT
from pathlib import Path
import importlib.util
import struct
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / (name + ".py"))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


class Tools(unittest.TestCase):
    def test_interleaved_estimates_use_both_neighboring_off_phases(self):
        benchmark = module("summarize_benchmark")
        text = []
        for i, preset in enumerate((0, 1, 0, 2, 0, 3, 0, 4, 0)):
            text.append(f"phase,{i},preset,{preset},menu,-1\ncpu,600,80,90,100,200\n"
                        f"gpu_query,600,{1000 + i * 10 + preset * 20},1100,1200,1300\n"
                        "fps,60\noverlay_cpu_percent,0.48\nprocess_cpu_percent,5\ndraw_calls,1")
        parsed = benchmark.parse("\n".join(text))
        output = list(benchmark.rows(parsed))
        self.assertEqual(output[7]["gpu_query_delta_estimate_us"], 80)
        self.assertEqual(output[7]["fps_change_percent"], 0)
        threaded = benchmark.parse("\n".join(text) +
                                   "\nsampler_cpu_us,7000\nsampler_window_s,10\n"
                                   "sampler_core_percent,0.07\nlibrary_core_percent,0.55\n"
                                   "sampler_threaded,1\nsampler_nice,19\nsampler_policy,0\n"
                                   "warmup_cpu_max_us,400")
        latest = list(benchmark.rows(threaded))[8]
        self.assertEqual(latest["sampler_core_percent"], 0.07)
        self.assertEqual(latest["library_core_percent"], 0.55)
        self.assertEqual(latest["warmup_cpu_max_us"], 400)
        self.assertEqual(output[7]["sampler_core_percent"], "")
        with self.assertRaises(ValueError):
            benchmark.parse("\n".join(text[:-1]))

    def test_cleanup_preserves_other_bytes_comments_and_cdata(self):
        cleanup = module("remove_gamelist_entry")
        self.assertTrue(cleanup.empty_gamelist(b'<gameList>\n</gameList>'))
        self.assertFalse(cleanup.empty_gamelist(b'<gameList><!--keep--></gameList>'))
        other = b'  <game><path>./Keep.sh</path><desc><![CDATA[<game></game>]]></desc></game>\n'
        comment = b'<!-- <game><path>./SPO Verify-123.sh</path></game> -->\n'
        own = b'  <game id="temporary"><path>./SPO Verify-123.sh</path><name>Temporary</name></game>\n'
        original = b'<gameList>\n' + comment + other + own + b'</gameList>\n'
        expected = b'<gameList>\n' + comment + other + b'</gameList>\n'
        self.assertEqual(cleanup.remove_entry(original, '/userdata/roms/ports/SPO Verify-123.sh'), expected)
        self.assertEqual(cleanup.remove_entry(expected, '/userdata/roms/ports/SPO Verify-123.sh'), expected)
        with self.assertRaises(ValueError):
            cleanup.remove_entry(original, 'Keep.sh')

    def test_bmp_lossless_color_alpha_and_row_order(self):
        codec = module("bmp_to_png")
        header = bytearray(108)
        struct.pack_into('<IiiHHI', header, 0, 108, 1, 2, 1, 32, 3)
        struct.pack_into('<IIII', header, 40, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000)
        data = b'BM' + struct.pack('<IHHI', 130, 0, 0, 122) + header
        data += bytes((0, 255, 0, 127, 0, 0, 255, 255))
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'capture.bmp'
            target = Path(directory) / 'capture.png'
            source.write_bytes(data)
            codec.convert(source, target)
            png = target.read_bytes()
            self.assertEqual(png[:8], b'\x89PNG\r\n\x1a\n')
            offset = 8
            compressed = bytearray()
            while offset < len(png):
                size, = struct.unpack_from('>I', png, offset)
                kind = png[offset + 4:offset + 8]
                payload = png[offset + 8:offset + 8 + size]
                crc, = struct.unpack_from('>I', png, offset + 8 + size)
                self.assertEqual(crc, zlib.crc32(kind + payload))
                if kind == b'IDAT':
                    compressed.extend(payload)
                offset += 12 + size
            self.assertEqual(zlib.decompress(compressed), bytes((0, 255, 0, 0, 255, 0, 0, 255, 0, 127)))


if __name__ == '__main__':
    unittest.main()

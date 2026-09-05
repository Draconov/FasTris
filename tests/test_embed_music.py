#!/usr/bin/env python3
from __future__ import annotations
import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "embed_music.py"

spec = importlib.util.spec_from_file_location("embed_music", SCRIPT)
if spec is None or spec.loader is None:
    raise RuntimeError("cannot load embed_music.py")
embed_music = importlib.util.module_from_spec(spec)
spec.loader.exec_module(embed_music)

class EmbedMusicTests(unittest.TestCase):
    def test_each_supported_extension_is_discovered(self):
        for ext, expected in [("ogg", "Ogg"), ("mp3", "Mp3"), ("wav", "Wav")]:
            with self.subTest(ext=ext), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                path = root / f"menu.{ext}"
                path.write_bytes(b"abc")
                found = embed_music.discover_slot(root, "menu")
                self.assertEqual(found, path)
                self.assertEqual(embed_music.format_name(found), expected)

    def test_duplicate_formats_are_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "menu.ogg").write_bytes(b"a")
            (root / "menu.mp3").write_bytes(b"b")
            with self.assertRaisesRegex(ValueError, "exactly one"):
                embed_music.discover_slot(root, "menu")

    def test_generated_cpp_contains_embedded_bytes_and_format_metadata(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            menu = root / "menu.ogg"; menu.write_bytes(bytes([0, 1, 254, 255]))
            gameplay = root / "gameplay.mp3"; gameplay.write_bytes(bytes([2, 3]))
            intense = root / "intense.wav"; intense.write_bytes(bytes([4, 5, 6]))
            output = root / "music_assets.cpp"
            embed_music.emit_cpp(output, {"menu": menu, "gameplay": gameplay, "intense": intense})
            text = output.read_text(encoding="utf-8")
            self.assertIn("MusicFormat::Ogg", text)
            self.assertIn("MusicFormat::Mp3", text)
            self.assertIn("MusicFormat::Wav", text)
            self.assertIn("0x00, 0x01, 0xfe, 0xff", text)
            self.assertIn("embeddedMusicAsset(MusicTrack track)", text)

if __name__ == "__main__":
    unittest.main()

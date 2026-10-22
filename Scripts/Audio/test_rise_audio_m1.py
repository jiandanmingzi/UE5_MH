"""Only offline index/WAV checks; no game, UE, network or external executable."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import wave

spec = importlib.util.spec_from_file_location("rise_audio_m1", Path(__file__).with_name("rise_audio_m1.py"))
audio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audio)


def chunk(tag, data):
    return struct.pack("<4sI", tag, len(data)) + data


class AudioIndexTests(unittest.TestCase):
    def test_bank_media_offset(self):
        data = chunk(b"BKHD", struct.pack("<II", 140, 123))
        data += chunk(b"DIDX", struct.pack("<III", 456, 2, 4)) + chunk(b"DATA", b"..RIFF")
        result = audio.bank_index(data)
        media = result["media"][0]
        self.assertEqual(data[media["offset"]:media["offset"] + media["size"]], b"RIFF")
        self.assertEqual((result["version"], result["bank_id"], media["media_id"]), (140, 123, 456))

    def test_empty_bank(self):
        self.assertEqual(audio.bank_index(chunk(b"BKHD", struct.pack("<II", 140, 5)))["media"], [])

    def test_truncated_chunk(self):
        with self.assertRaises(ValueError):
            audio.bank_index(b"BKHD" + struct.pack("<I", 24) + b"x")

    def test_outside_data(self):
        data = chunk(b"BKHD", struct.pack("<II", 140, 1))
        data += chunk(b"DIDX", struct.pack("<III", 2, 100, 4)) + chunk(b"DATA", b"RIFF")
        with self.assertRaises(ValueError):
            audio.bank_index(data)

    def test_indented_txtp_dependencies(self):
        path = audio.PROJECT / "Saved/Audio/RiseIG/Events/example/example.txtp"
        result = audio.render_dependencies(path, [" ../../Banks/hit_com_media.bnk #s3 #i", "group = -R3>1"])
        self.assertEqual(result[0]["path"], (path.parent / "../../Banks/hit_com_media.bnk").resolve().as_posix())
        self.assertEqual(len(result), 1)

    def test_wave_not_empty_file_only(self):
        temp_root = audio.PROJECT / "Saved/Audio/RiseIG/Tests"
        temp_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=temp_root) as directory:
            path = Path(directory) / "test.wav"
            with wave.open(str(path), "wb") as stream:
                stream.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
                stream.writeframes(struct.pack("<hhh", 0, 1000, -1000))
            result = audio.wav_metrics(path)
            self.assertTrue(result["non_silent"])
            self.assertEqual(result["peak"], 1000)
            self.assertEqual(result["sample_rate"], 48000)


if __name__ == "__main__":
    unittest.main()

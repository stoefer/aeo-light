# -*- coding: utf-8 -*-
"""Een SRT vervangt de spraakherkenning. Zonder spraak komt die in de film."""
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import TeleCine_AudioTranslator as appmod
import telecine_media


SAMPLE = """\ufeff1
00:00:01,250 --> 00:00:02,500
Bonjour
dit is

2
00:00:03.000 --> 00:00:04.000


3
01:05:30,5 --> 01:05:31,250
Daarna
gaat het verder.
"""


class ReadSrtTests(unittest.TestCase):
    def test_times_stay_and_empty_cues_drop_out(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "bron.srt"
            path.write_text(SAMPLE.replace("\n", "\r\n"), encoding="utf-8")
            cues = appmod.read_srt(path)
        self.assertEqual(len(cues), 2)
        self.assertEqual(cues[0]["start"], 1.25)
        self.assertEqual(cues[0]["end"], 2.5)
        self.assertEqual(cues[0]["text"], "Bonjour dit is")
        self.assertEqual(cues[1]["start"], 1 * 3600 + 5 * 60 + 30.5)
        self.assertEqual(cues[1]["end"], 1 * 3600 + 5 * 60 + 31.25)
        self.assertEqual(cues[1]["text"], "Daarna gaat het verder.")

    def test_roundtrip_keeps_the_times(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "nl.srt"
            original = [
                {"start": 1.25, "end": 2.5, "text": "Goeiedag"},
                {"start": 65.5, "end": 66.25, "translation": "verder"},
            ]
            appmod.write_srt(original, path, "text")
            appmod.write_srt(original, Path(folder) / "leeg-overgeslagen.srt", "translation")
            back = appmod.read_srt(path)
            dutch = appmod.read_srt(Path(folder) / "leeg-overgeslagen.srt")
        self.assertEqual(back[0]["start"], 1.25)
        self.assertEqual(back[0]["end"], 2.5)
        self.assertEqual(back[0]["text"], "Goeiedag")
        self.assertEqual(dutch[0]["text"], "verder")
        self.assertEqual(dutch[0]["start"], 65.5)

    def test_a_file_without_cues_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "leeg.srt"
            path.write_text("geen tijdcode\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                appmod.read_srt(path)


class SubtitleMuxCommandTests(unittest.TestCase):
    def test_video_and_audio_are_copied_and_the_subtitle_is_dutch(self):
        cmd = telecine_media.subtitle_mux_command("ffmpeg", "film.mp4", "nl.srt", "deel.mp4", True)
        self.assertIn("-c:v", cmd)
        self.assertEqual(cmd[cmd.index("-c:v") + 1], "copy")
        self.assertIn("-c:a", cmd)
        self.assertEqual(cmd[cmd.index("-c:a") + 1], "copy")
        self.assertEqual(cmd[cmd.index("-c:s") + 1], "mov_text")
        self.assertIn("language=nld", cmd)
        self.assertIn("default", cmd)
        self.assertEqual(cmd[cmd.index("-map") + 1], "0:v:0")

    def test_a_silent_film_skips_the_audio_map(self):
        cmd = telecine_media.subtitle_mux_command("ffmpeg", "film.mp4", "nl.srt", "deel.mp4", False)
        self.assertNotIn("-c:a", cmd)
        self.assertNotIn("0:a:0", cmd)
        self.assertIn("0:v:0", cmd)


class SpeechOptionTests(unittest.TestCase):
    def setUp(self):
        self.app = appmod.App()
        self.app.withdraw()

    def tearDown(self):
        try:
            pending = self.app.tk.call("after", "info")
        except Exception:
            pending = ()
        for item in pending:
            try:
                self.app.after_cancel(item)
            except Exception:
                pass
        try:
            self.app.destroy()
        except appmod.tk.TclError:
            pass

    def test_speech_is_on_by_default_and_an_old_job_keeps_it(self):
        self.assertTrue(self.app.make_speech.get())
        self.assertEqual(self.app.speech_check.cget("text"), "Vlaamse spraak maken")
        state = self.app.default_state("/tmp/telecine-bron.wav")
        self.assertTrue(state["make_speech"])
        self.assertEqual(state["srt_path"], "")
        self.app.make_speech.set(False)
        self.app.srt_path.set("/tmp/bron.srt")
        stored = self.app.default_state("/tmp/telecine-bron.wav")
        self.assertFalse(stored["make_speech"])
        self.assertEqual(stored["srt_path"], "/tmp/bron.srt")
        self.app.apply_job_state({"source_language": "fr"})
        self.assertTrue(self.app.make_speech.get())
        self.assertEqual(self.app.srt_path.get(), "")

    def test_without_speech_the_continue_button_places_subtitles(self):
        self.app._speech_enabled = False
        seen = {}

        def worker():
            try:
                self.app.wait_for_translation_edit(
                    [{"start": 1.0, "end": 2.0, "text": "Hello", "translation": "Hallo"}],
                    None,
                )
            except Exception as exc:
                seen["error"] = exc

        import threading
        thread = threading.Thread(target=worker, daemon=True)
        self.app.worker = thread
        thread.start()
        deadline = __import__("time").time() + 5
        while __import__("time").time() < deadline and self.app._transcript_editor is None:
            self.app.update()
            __import__("time").sleep(0.02)
        editor = self.app._transcript_editor
        self.assertIsNotNone(editor)
        self.assertEqual(editor.continue_button.cget("text"), "Ondertitels plaatsen")
        editor.close_for_stop()
        thread.join(timeout=2)

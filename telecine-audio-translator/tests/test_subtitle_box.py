# -*- coding: utf-8 -*-
"""De Nederlandse ondertitel staat op een instelbare grijze balk."""
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import TeleCine_AudioTranslator as appmod
import telecine_media


class SubtitleBoxTests(unittest.TestCase):
    def test_gray_and_transparency_become_an_ass_colour(self):
        self.assertEqual(telecine_media.ass_colour(40, 0), "&H00282828")
        self.assertEqual(telecine_media.ass_colour(40, 100), "&HFF282828")
        self.assertEqual(telecine_media.ass_colour(255, 50), "&H80FFFFFF")

    def test_the_box_style_covers_the_line_at_its_own_time(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "nl.ass"
            telecine_media.write_subtitle_box(
                [
                    {"start": 1.25, "end": 2.5, "translation": "Goeiedag {daar}"},
                    {"start": 3.0, "end": 4.0, "translation": "   "},
                ],
                path,
                gray=40,
                transparency=0,
                width=1280,
                height=720,
                text_key="translation",
            )
            text = path.read_text(encoding="utf-8")
        self.assertIn("PlayResX: 1280", text)
        self.assertIn("PlayResY: 720", text)
        self.assertIn(",3,", text)
        self.assertIn("&H00282828", text)
        self.assertIn("Dialogue: 0,0:00:01.25,0:00:02.50,Dutch,,0,0,0,,Goeiedag \\{daar\\}", text)
        self.assertEqual(text.count("Dialogue:"), 1)

    def test_the_burn_command_draws_the_box_and_keeps_the_sound(self):
        plain = telecine_media.burn_subtitle_command("ffmpeg", "film.mp4", "balk.ass", "deel.mp4")
        self.assertIn("subtitles=filename=", " ".join(plain))
        self.assertEqual(plain[plain.index("-c:v") + 1], "libx264")
        self.assertEqual(plain[plain.index("-c:a") + 1], "copy")
        voiced = telecine_media.burn_subtitle_command("ffmpeg", "film.mp4", "balk.ass", "deel.mp4", wav="stem.wav")
        self.assertEqual(voiced[voiced.index("-c:a") + 1], "aac")
        self.assertIn("stem.wav", voiced)

    def test_the_sliders_are_stored_with_the_job(self):
        app = appmod.App()
        app.withdraw()
        try:
            self.assertEqual(app.subtitle_gray.get(), 40)
            self.assertEqual(app.subtitle_transparency.get(), 15)
            app.subtitle_gray.set(80)
            app.subtitle_transparency.set(60)
            self.assertEqual(app.subtitle_gray_text.get(), "80")
            self.assertEqual(app.subtitle_transparency_text.get(), "60%")
            state = app.default_state("/tmp/telecine-bron.wav")
            self.assertEqual(state["subtitle_gray"], 80)
            self.assertEqual(state["subtitle_transparency"], 60)
            app.subtitle_gray.set(40)
            app.apply_job_state({"source_language": "fr"})
            self.assertEqual(app.subtitle_gray.get(), 40)
            app.apply_job_state({"source_language": "fr", "subtitle_gray": 12, "subtitle_transparency": 90})
            self.assertEqual(app.subtitle_gray.get(), 12)
            self.assertEqual(app.subtitle_transparency.get(), 90)
        finally:
            try:
                pending = app.tk.call("after", "info")
            except Exception:
                pending = ()
            for item in pending:
                try:
                    app.after_cancel(item)
                except Exception:
                    pass
            app.destroy()

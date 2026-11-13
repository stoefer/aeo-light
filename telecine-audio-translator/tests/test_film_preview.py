# -*- coding: utf-8 -*-
"""De filmschuif kiest een frame als start of einde van het te vertalen stuk."""
import sys
import tempfile
import unittest
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import TeleCine_AudioTranslator as appmod
import telecine_media


def cancel_after(widget):
    try:
        pending = widget.tk.call("after", "info")
    except Exception:
        return
    for item in pending:
        try:
            widget.after_cancel(item)
        except Exception:
            pass


class RangeMathTests(unittest.TestCase):
    def test_a_frame_becomes_a_start_or_the_end_of_that_frame(self):
        self.assertEqual(appmod.point_seconds(0, 25, 10, "start"), 0.0)
        self.assertEqual(appmod.point_seconds(24, 25, 10, "start"), 0.96)
        self.assertEqual(appmod.point_seconds(24, 25, 10, "end"), 1.0)
        self.assertEqual(appmod.point_seconds(249, 25, 10, "end"), 10.0)

    def test_the_end_stays_after_the_start(self):
        start, end, error = appmod.assign_range_point(None, None, "start", 1.5)
        self.assertEqual((start, end, error), (1.5, None, ""))
        start, end, error = appmod.assign_range_point(1.5, None, "end", 1.5)
        self.assertEqual(error, "Het eindpunt moet na het startpunt liggen.")
        self.assertEqual(end, None)
        start, end, error = appmod.assign_range_point(1.5, None, "end", 4)
        self.assertEqual((start, end, error), (1.5, 4.0, ""))

    def test_cues_outside_the_range_drop_and_the_edge_is_cut(self):
        segments = [
            {"start": 0.0, "end": 1.0, "text": "ervoor"},
            {"start": 1.2, "end": 3.5, "text": "erin"},
            {"start": 4.0, "end": 5.0, "text": "erna"},
        ]
        kept = appmod.limit_segments(segments, 1.0, 4.0)
        self.assertEqual(len(kept), 1)
        self.assertEqual(kept[0]["text"], "erin")
        self.assertEqual(kept[0]["start"], 1.2)
        self.assertEqual(kept[0]["end"], 3.5)
        cut = appmod.limit_segments([{"start": 0.5, "end": 2.0, "text": "over"}], 1.0, 1.4)
        self.assertEqual(cut[0]["start"], 1.0)
        self.assertEqual(cut[0]["end"], 1.4)

    def test_probe_json_counts_frames(self):
        timeline = telecine_media.timeline_from_probe({
            "streams": [{"avg_frame_rate": "24000/1001", "duration": "2.0", "nb_frames": "48"}],
        })
        self.assertEqual(timeline["frames"], 48)
        self.assertAlmostEqual(timeline["fps"], 24000 / 1001, places=4)
        self.assertTrue(timeline["has_picture"])

    def test_an_accurate_frame_command_seeks_after_the_input(self):
        fast = telecine_media.extract_frame_command("ffmpeg", "film.mp4", 1.25, "frame.png", False)
        accurate = telecine_media.extract_frame_command("ffmpeg", "film.mp4", 1.25, "frame.png", True)
        self.assertLess(fast.index("-ss"), fast.index("-i"))
        self.assertGreater(accurate.index("-ss"), accurate.index("-i"))
        self.assertIn("1.250", accurate)
        self.assertEqual(accurate[accurate.index("-frames:v") + 1], "1")

    def test_a_clip_command_keeps_the_span_on_the_source_clock(self):
        cmd = telecine_media.extract_wav_command("ffmpeg", "film.mp4", "deel.wav", 12.5, 20)
        self.assertEqual(cmd[cmd.index("-ss") + 1], "12.500")
        self.assertEqual(cmd[cmd.index("-t") + 1], "7.500")
        self.assertLess(cmd.index("-ss"), cmd.index("-i"))


class PreviewWindowTests(unittest.TestCase):
    def setUp(self):
        self.app = appmod.App()
        self.app.withdraw()
        self.folder = tempfile.TemporaryDirectory()
        self.wav = Path(self.folder.name) / "bron.wav"
        with wave.open(str(self.wav), "w") as handle:
            handle.setnchannels(1)
            handle.setsampwidth(2)
            handle.setframerate(8000)
            handle.writeframes(b"\x00\x00" * 8000)

    def tearDown(self):
        preview = getattr(self.app, "_film_preview", None)
        if preview is not None:
            cancel_after(preview)
            try:
                preview._close()
            except Exception:
                pass
        cancel_after(self.app)
        try:
            self.app.destroy()
        except appmod.tk.TclError:
            pass
        self.folder.cleanup()

    def test_the_slider_sets_a_start_frame_and_an_end_frame(self):
        self.app.source.set(str(self.wav))
        self.app.open_preview()
        preview = self.app._film_preview
        self.assertIsNotNone(preview)
        self.assertEqual(preview.title(), "Film bekijken")
        self.assertEqual(preview.slider.cget("to"), preview.timeline["frames"] - 1)
        preview.slider.set(0)
        preview._set_start()
        preview.slider.set(preview.timeline["frames"] - 1)
        preview._set_end()
        self.assertEqual(self.app.range_start.get(), "0.000")
        self.assertEqual(float(self.app.range_end.get()), preview.timeline["duration"])
        self.assertIn("Bereik:", self.app.range_text.get())
        state = self.app.default_state(str(self.wav))
        self.assertEqual(state["range_start"], "0.000")
        self.assertNotEqual(state["range_end"], "")
        preview._clear()
        self.assertEqual(self.app.range_text.get(), "Bereik: de hele film")
        self.assertEqual(self.app.default_state(str(self.wav))["range_start"], "")

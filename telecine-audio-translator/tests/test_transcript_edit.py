# -*- coding: utf-8 -*-
"""De bewerkte transcriptie houdt de herkende begin- en eindtijd."""
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import TeleCine_AudioTranslator as appmod


SEGMENTS = [
    {"start": 1.25, "end": 2.5, "text": "Dit is"},
    {"start": 3.0, "end": 4.0, "text": "een zin."},
    {"start": 65.5, "end": 66.25, "text": "Daarna gaat het verder."},
]

TRANSLATED = [
    {"start": 1.25, "end": 2.5, "text": "Bonjour", "translation": "Goeiedag dit is"},
    {"start": 3.0, "end": 4.0, "text": "une phrase.", "translation": "een zin."},
    {"start": 65.5, "end": 66.25, "text": "Ensuite.", "translation": "Daarna gaat het verder."},
]


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


class TimingTests(unittest.TestCase):
    def test_edit_keeps_start_and_end(self):
        edited = appmod.apply_transcript_edits(
            SEGMENTS,
            ["Een gecorrigeerde zin.", "een zin.", "  Daarna   gaat\nhet verder.  "],
        )
        self.assertEqual([(item["start"], item["end"]) for item in edited], [(item["start"], item["end"]) for item in SEGMENTS])
        self.assertEqual(edited[0]["text"], "Een gecorrigeerde zin.")
        self.assertEqual(edited[2]["text"], "Daarna gaat het verder.")

    def test_edit_rejects_a_different_number_of_sentences(self):
        with self.assertRaises(ValueError):
            appmod.apply_transcript_edits(SEGMENTS, ["alleen deze"])

    def test_merge_uses_the_outer_recognition_times(self):
        merged = appmod.merge_transcript_segments(SEGMENTS, 0)
        self.assertEqual(len(merged), 2)
        self.assertEqual(merged[0]["start"], 1.25)
        self.assertEqual(merged[0]["end"], 4.0)
        self.assertEqual(merged[0]["text"], "Dit is een zin.")
        self.assertEqual(merged[1]["start"], 65.5)
        self.assertEqual(merged[1]["end"], 66.25)
        self.assertEqual(merged[1]["text"], "Daarna gaat het verder.")

    def test_empty_sentence_drops_out_of_srt_without_moving_the_next_cue(self):
        segments = [
            {"start": 1.25, "end": 2.5, "text": "Eerste zin."},
            {"start": 3.0, "end": 4.0, "text": "   "},
            {"start": 65.5, "end": 66.25, "text": "Derde zin."},
        ]
        translated = [
            {"start": item["start"], "end": item["end"], "translation": item["text"]}
            for item in segments
        ]
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "bron.srt"
            dutch = Path(folder) / "nl.srt"
            appmod.write_srt(segments, source, "text")
            appmod.write_srt(translated, dutch, "translation")
            expected = (
                "1\n"
                "00:00:01,250 --> 00:00:02,500\n"
                "Eerste zin.\n"
                "\n"
                "2\n"
                "00:01:05,500 --> 00:01:06,250\n"
                "Derde zin.\n\n"
            )
            self.assertEqual(source.read_text(encoding="utf-8"), expected)
            self.assertEqual(dutch.read_text(encoding="utf-8"), expected)
        self.assertEqual(appmod.format_timed_text(segments), "[00:00:01.250 - 00:00:02.500] Eerste zin.\n[00:01:05.500 - 00:01:06.250] Derde zin.")

    def test_dutch_edit_keeps_the_source_sentence_and_the_times(self):
        edited = appmod.apply_transcript_edits(
            TRANSLATED,
            ["Goedendag.", "een zin.", "Daarna gaat het verder."],
            "translation",
        )
        self.assertEqual(edited[0]["translation"], "Goedendag.")
        self.assertEqual(edited[0]["text"], "Bonjour")
        self.assertEqual([(item["start"], item["end"]) for item in edited], [(item["start"], item["end"]) for item in TRANSLATED])
        self.assertEqual(edited[2]["text"], "Ensuite.")

    def test_dutch_merge_keeps_the_outer_times_and_both_languages(self):
        merged = appmod.merge_transcript_segments(TRANSLATED, 0, "translation")
        self.assertEqual(len(merged), 2)
        self.assertEqual(merged[0]["start"], 1.25)
        self.assertEqual(merged[0]["end"], 4.0)
        self.assertEqual(merged[0]["translation"], "Goeiedag dit is een zin.")
        self.assertEqual(merged[0]["text"], "Bonjour une phrase.")
        self.assertEqual(merged[1]["start"], 65.5)
        self.assertEqual(merged[1]["end"], 66.25)
        self.assertEqual(merged[1]["translation"], "Daarna gaat het verder.")

    def test_srt_timestamp_rounds_to_the_nearest_millisecond(self):
        self.assertEqual(appmod.srt_timestamp(1.25), "00:00:01,250")
        self.assertEqual(appmod.srt_timestamp(3661.5), "01:01:01,500")
        self.assertEqual(appmod.srt_timestamp(0.9996), "00:00:01,000")


class EditorTests(unittest.TestCase):
    def setUp(self):
        self.root = appmod.tk.Tk()
        self.root.withdraw()
        self.confirmed = None
        self.cancelled = False
        self.editor = appmod.TranscriptEditor(
            self.root,
            SEGMENTS,
            on_confirm=self._confirm,
            on_cancel=self._cancel,
            audio_path=None,
        )

    def tearDown(self):
        cancel_after(self.editor)
        cancel_after(self.root)
        try:
            self.editor.destroy()
        except appmod.tk.TclError:
            pass
        self.root.destroy()

    def _confirm(self, edited):
        self.confirmed = edited

    def _cancel(self):
        self.cancelled = True

    def _pump(self, steps=5):
        for _ in range(steps):
            self.root.update()

    def test_confirm_keeps_every_time_and_uses_the_corrected_sentence(self):
        self._pump()
        self.editor._text_widgets[0].delete("1.0", "end")
        self.editor._text_widgets[0].insert("1.0", "Een betere zin.")
        self.editor._confirm()
        self.assertIsNotNone(self.confirmed)
        self.assertEqual(self.confirmed[0]["text"], "Een betere zin.")
        self.assertEqual([(item["start"], item["end"]) for item in self.confirmed], [(item["start"], item["end"]) for item in SEGMENTS])
        self.assertEqual(self.confirmed[1]["text"], "een zin.")
        self.assertEqual(self.confirmed[2]["text"], "Daarna gaat het verder.")

    def test_merge_and_undo_keep_the_remaining_time_codes(self):
        self._pump()
        self.editor._text_widgets[0].delete("1.0", "end")
        self.editor._text_widgets[0].insert("1.0", "Hallo")
        self.editor._merge(0)
        self.assertEqual(len(self.editor.segments), 2)
        self.assertEqual(self.editor.segments[0]["start"], 1.25)
        self.assertEqual(self.editor.segments[0]["end"], 4.0)
        self.assertEqual(self.editor.segments[0]["text"], "Hallo een zin.")
        self.assertEqual(self.editor.segments[1]["start"], 65.5)
        self.assertEqual(self.editor.segments[1]["end"], 66.25)
        label = self.editor.list_frame.winfo_children()[0].winfo_children()[1]
        self.assertIn("00:00:01.250", label.cget("text"))
        self.assertIn("00:00:04.000", label.cget("text"))
        self.editor._undo()
        self.assertEqual(len(self.editor.segments), 3)
        self.assertEqual(self.editor.segments[0]["text"], "Hallo")
        self.assertEqual(self.editor.segments[0]["end"], 2.5)
        self.assertEqual(self.editor.segments[1]["start"], 3.0)

    def test_restore_brings_back_the_recognised_wording(self):
        self._pump()
        self.editor._text_widgets[2].delete("1.0", "end")
        self.editor._text_widgets[2].insert("1.0", "Iets anders.")
        self.editor._restore()
        self.assertEqual(self.editor.segments[2]["text"], "Daarna gaat het verder.")
        self.assertEqual(self.editor.segments[2]["start"], 65.5)
        self.assertEqual(self.editor.segments[2]["end"], 66.25)

    def test_an_empty_transcript_does_not_continue(self):
        self._pump()
        shown = []
        original = appmod.messagebox.showerror
        appmod.messagebox.showerror = lambda *args, **kwargs: shown.append(args)
        try:
            for widget in self.editor._text_widgets:
                widget.delete("1.0", "end")
            self.editor._confirm()
        finally:
            appmod.messagebox.showerror = original
        self.assertTrue(shown)
        self.assertIsNone(self.confirmed)
        self.assertFalse(self.editor._closed)

    def test_stop_does_not_return_edited_text(self):
        self._pump()
        self.editor.close_for_stop()
        self.assertTrue(self.cancelled)
        self.assertIsNone(self.confirmed)


class DutchEditorTests(unittest.TestCase):
    def setUp(self):
        self.root = appmod.tk.Tk()
        self.root.withdraw()
        self.confirmed = None
        self.editor = appmod.TranscriptEditor(
            self.root,
            TRANSLATED,
            on_confirm=self._confirm,
            on_cancel=lambda: None,
            audio_path=None,
            field="translation",
        )

    def tearDown(self):
        cancel_after(self.editor)
        cancel_after(self.root)
        try:
            self.editor.destroy()
        except appmod.tk.TclError:
            pass
        self.root.destroy()

    def _confirm(self, edited):
        self.confirmed = edited

    def test_the_dutch_sentence_changes_and_the_source_and_times_stay(self):
        self.root.update()
        self.assertEqual(self.editor.title(), "Nederlandse vertaling bewerken")
        self.assertEqual(self.editor.continue_button.cget("text"), "Spraak starten")
        labels = [child.cget("text") for child in self.editor.list_frame.winfo_children()[0].winfo_children() if child.winfo_class() == "Label"]
        self.assertTrue(any(text.startswith("bron  Bonjour") for text in labels))
        self.assertEqual(self.editor._text_widgets[0].get("1.0", "end").strip(), "Goeiedag dit is")
        self.editor._text_widgets[0].delete("1.0", "end")
        self.editor._text_widgets[0].insert("1.0", "Goedendag.")
        self.editor._confirm()
        self.assertEqual(self.confirmed[0]["translation"], "Goedendag.")
        self.assertEqual(self.confirmed[0]["text"], "Bonjour")
        self.assertEqual(self.confirmed[0]["start"], 1.25)
        self.assertEqual(self.confirmed[0]["end"], 2.5)
        self.assertEqual(self.confirmed[1]["translation"], "een zin.")
        self.assertEqual(self.confirmed[2]["start"], 65.5)
        self.assertEqual(self.confirmed[2]["end"], 66.25)


class AppPauseTests(unittest.TestCase):
    def setUp(self):
        self.app = appmod.App()
        self.app.withdraw()

    def tearDown(self):
        cancel_after(self.app)
        editor = getattr(self.app, "_transcript_editor", None)
        if editor is not None:
            try:
                editor.close_for_stop()
            except Exception:
                pass
        try:
            self.app.update()
        except appmod.tk.TclError:
            pass
        cancel_after(self.app)
        try:
            self.app.destroy()
        except appmod.tk.TclError:
            pass

    def _wait_for(self, predicate, seconds=5):
        deadline = time.time() + seconds
        while time.time() < deadline:
            try:
                self.app.update()
            except appmod.tk.TclError:
                return False
            if predicate():
                return True
            time.sleep(0.02)
        return False

    def test_the_option_is_on_by_default_and_stored_with_the_job(self):
        self.assertTrue(self.app.edit_transcript.get())
        self.assertTrue(self.app.edit_translation.get())
        self.assertIn("Transcriptie bewerken", self.app.edit_check.cget("text"))
        self.assertIn("Nederlandse vertaling bewerken", self.app.translation_check.cget("text"))
        state = self.app.default_state("/tmp/telecine-bron.wav")
        self.assertTrue(state["edit_transcript"])
        self.assertTrue(state["edit_translation"])
        self.app.edit_translation.set(False)
        self.assertFalse(self.app.default_state("/tmp/telecine-bron.wav")["edit_translation"])

    def test_the_pipeline_waits_and_then_keeps_the_corrected_timing(self):
        holder = {}

        def worker():
            try:
                holder["out"] = self.app.wait_for_transcript_edit(SEGMENTS, None)
            except Exception as exc:
                holder["error"] = exc

        thread = threading.Thread(target=worker, daemon=True)
        self.app.worker = thread
        thread.start()
        self.assertTrue(self._wait_for(lambda: self.app._transcript_editor is not None))
        editor = self.app._transcript_editor
        editor._text_widgets[1].delete("1.0", "end")
        editor._text_widgets[1].insert("1.0", "de verbeterde zin.")
        editor._confirm()
        self.assertTrue(self._wait_for(lambda: "out" in holder or "error" in holder))
        self.assertNotIn("error", holder)
        edited = holder["out"]
        self.assertEqual(edited[1]["text"], "de verbeterde zin.")
        self.assertEqual([(item["start"], item["end"]) for item in edited], [(item["start"], item["end"]) for item in SEGMENTS])
        self.assertFalse(self.app.stop_event.is_set())

    def test_the_pipeline_waits_for_the_dutch_text_and_keeps_the_timing(self):
        holder = {}

        def worker():
            try:
                holder["out"] = self.app.wait_for_translation_edit(TRANSLATED, None)
            except Exception as exc:
                holder["error"] = exc

        thread = threading.Thread(target=worker, daemon=True)
        self.app.worker = thread
        thread.start()
        self.assertTrue(self._wait_for(lambda: self.app._transcript_editor is not None))
        editor = self.app._transcript_editor
        self.assertEqual(editor.field, "translation")
        editor._text_widgets[1].delete("1.0", "end")
        editor._text_widgets[1].insert("1.0", "de verbeterde Nederlandse zin.")
        editor._confirm()
        self.assertTrue(self._wait_for(lambda: "out" in holder or "error" in holder))
        self.assertNotIn("error", holder)
        edited = holder["out"]
        self.assertEqual(edited[1]["translation"], "de verbeterde Nederlandse zin.")
        self.assertEqual(edited[1]["text"], "une phrase.")
        self.assertEqual([(item["start"], item["end"]) for item in edited], [(item["start"], item["end"]) for item in TRANSLATED])

    def test_stop_during_editing_cancels_the_translation(self):
        holder = {}

        def worker():
            try:
                holder["out"] = self.app.wait_for_transcript_edit(SEGMENTS, None)
            except appmod.StoppedByUser:
                holder["stopped"] = True
            except Exception as exc:
                holder["error"] = exc

        thread = threading.Thread(target=worker, daemon=True)
        self.app.worker = thread
        thread.start()
        self.assertTrue(self._wait_for(lambda: self.app._transcript_editor is not None))
        self.app.request_stop()
        self.assertTrue(self._wait_for(lambda: "stopped" in holder or "error" in holder or "out" in holder))
        self.assertTrue(holder.get("stopped"))
        self.assertNotIn("out", holder)


class AppearanceTests(unittest.TestCase):
    def test_startup_height_fits_the_content_until_the_screen_is_full(self):
        width, height = appmod.startup_window_size(1920, 1080, 1500)
        self.assertEqual(width, 1180)
        self.assertEqual(height, 1080 - 88)
        width, height = appmod.startup_window_size(2560, 1440, 1200)
        self.assertEqual(height, 1200)
        width, height = appmod.startup_window_size(1280, 800, 400)
        self.assertEqual(width, 1180)
        self.assertEqual(height, 640)

    def test_appearance_choice_is_in_the_settings_and_switches_colors(self):
        app = appmod.App()
        app.update()
        try:
            self.assertEqual(list(app.theme_box.cget("values")), ["Licht", "Donker"])
            self.assertEqual(app.theme_box.winfo_manager(), "grid")
            app.appearance.set("Donker")
            app.apply_appearance()
            dark = appmod.APPEARANCE_COLORS["Donker"]
            self.assertEqual(app.cget("bg"), dark["bg"])
            self.assertEqual(app.steps.cget("bg"), dark["field"])
            self.assertEqual(app.steps.cget("fg"), dark["fg"])
            self.assertEqual(app.meters["input"].peak_canvas.cget("bg"), dark["meter"])
            app.appearance.set("Licht")
            app.apply_appearance()
            light = appmod.APPEARANCE_COLORS["Licht"]
            self.assertEqual(app.cget("bg"), light["bg"])
            self.assertEqual(app.steps.cget("bg"), light["field"])
        finally:
            cancel_after(app)
            app.destroy()

    def test_appearance_is_remembered(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "weergave.json"
            appmod.save_appearance("Donker", path)
            self.assertEqual(appmod.load_appearance(path), "Donker")
            self.assertEqual(appmod.load_appearance(Path(folder) / "ontbreekt.json"), "Licht")


if __name__ == "__main__":
    unittest.main()

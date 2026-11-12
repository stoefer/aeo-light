# -*- coding: utf-8 -*-
"""Na een release gaat het nummer één stap omhoog, en niet nog een keer."""
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import verhoog_versie


class VersionBumpTests(unittest.TestCase):
    def test_the_last_part_grows_and_keeps_its_width(self):
        self.assertEqual(verhoog_versie.bump("0.9.01"), "0.9.02")
        self.assertEqual(verhoog_versie.bump("0.9.09"), "0.9.10")

    def test_a_second_release_uses_the_next_number(self):
        self.assertEqual(verhoog_versie.bump(verhoog_versie.bump("0.9.01")), "0.9.03")

    def test_unknown_text_is_rejected(self):
        with self.assertRaises(ValueError):
            verhoog_versie.bump("v0.9.01")

    def test_only_the_released_number_is_bumped_again(self):
        self.assertTrue(verhoog_versie.should_bump_after_release("0.9.01\n", "0.9.01"))
        self.assertFalse(verhoog_versie.should_bump_after_release("0.9.02", "0.9.01"))

    def test_files_move_to_the_next_number_once(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "VERSION").write_text("0.9.01\n", encoding="utf-8")
            (root / "README.md").write_text("# App\n\nVersie **0.9.01**. Het venster.\n", encoding="utf-8")
            self.assertEqual(verhoog_versie.bump_files(root), "0.9.02")
            self.assertEqual((root / "VERSION").read_text(encoding="utf-8"), "0.9.02\n")
            readme = (root / "README.md").read_text(encoding="utf-8")
            self.assertIn("Versie **0.9.02**.", readme)
            self.assertNotIn("Versie **0.9.01**.", readme)
            self.assertFalse(verhoog_versie.should_bump_after_release((root / "VERSION").read_text(encoding="utf-8"), "0.9.01"))


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3

import pathlib
import subprocess
import sys
import tempfile
import textwrap
import unittest

import yaml


SCRIPT_PATH = pathlib.Path(__file__).with_name("migrate_to_per_entry.py")


class MigrateToPerEntryTest(unittest.TestCase):

    def _write_test_changelog_files(self, root: pathlib.Path, date_value: str = "Pending") -> None:
        changelogs_dir = root / "changelogs"
        changelogs_dir.mkdir(parents=True, exist_ok=True)
        (changelogs_dir / "sections.yaml").write_text(textwrap.dedent("""\
            bug_fixes:
              title: Bug fixes
            new_features:
              title: New features
            """))
        (changelogs_dir / "current.yaml").write_text(
            textwrap.dedent(f"""\
            date: {date_value}
            bug_fixes:
            - area: router
              change: |
                Fix issue in router. Additional details.
            - area: router
              change: |
                Fix issue in router. Another detail.
            new_features:
            - area: extensions/filters/http/oauth2
              change: |
                Add nested area support for oauth2 filter feature and behavior that goes beyond sixty characters for slug input.
                Keep second line as-is.
            """))

    def test_migration_generates_entry_files_and_rewrites_yaml(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            root = pathlib.Path(tmpdir)
            self._write_test_changelog_files(root)

            subprocess.run(
                [sys.executable, str(SCRIPT_PATH), "--root", str(root)],
                check=True,
            )

            changelogs_dir = root / "changelogs"
            self.assertEqual((changelogs_dir / "current.yaml").read_text(), "date: Pending\n")

            router_entry = changelogs_dir / "current" / "bug_fixes" / "router__fix-issue-in-router.rst"
            router_entry_2 = changelogs_dir / "current" / "bug_fixes" / "router__fix-issue-in-router-2.rst"
            nested_entry = (
                changelogs_dir
                / "current"
                / "new_features"
                / "extensions~filters~http~oauth2__add-nested-area-support-for-oauth2.rst")

            self.assertTrue(router_entry.exists())
            self.assertTrue(router_entry_2.exists())
            self.assertTrue(nested_entry.exists())

            self.assertEqual(router_entry.read_text(), "Fix issue in router. Additional details.\n")
            self.assertEqual(router_entry_2.read_text(), "Fix issue in router. Another detail.\n")
            self.assertEqual(
                nested_entry.read_text(),
                "Add nested area support for oauth2 filter feature and behavior that goes beyond sixty characters for slug input.\n"
                "Keep second line as-is.\n")

            # The old `sections.yaml` is superseded by `changelogs.yaml`.
            self.assertFalse((changelogs_dir / "sections.yaml").exists())
            self.assertFalse((changelogs_dir / "areas.yaml").exists())
            self.assertTrue((changelogs_dir / "changelogs.yaml").exists())

            config = yaml.safe_load((changelogs_dir / "changelogs.yaml").read_text())
            self.assertEqual(
                config,
                {
                    "sections": {
                        "bug_fixes": {"title": "Bug fixes"},
                        "new_features": {"title": "New features"},
                    },
                    "areas": {
                        "extensions/filters/http/oauth2": {
                            "title": "extensions/filters/http/oauth2",
                        },
                        "router": {"title": "router"},
                    },
                })
            # Header comments are preserved.
            self.assertIn(
                "# NB: this file is the canonical changelog config",
                (changelogs_dir / "changelogs.yaml").read_text())

    def test_migration_preserves_non_pending_date(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            root = pathlib.Path(tmpdir)
            self._write_test_changelog_files(root, date_value="January 1, 2026")

            subprocess.run(
                [sys.executable, str(SCRIPT_PATH), "--root", str(root)],
                check=True,
            )

            data = yaml.safe_load((root / "changelogs" / "current.yaml").read_text())
            self.assertEqual(data, {"date": "January 1, 2026"})


if __name__ == "__main__":
    unittest.main()

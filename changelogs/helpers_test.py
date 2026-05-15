import io
import tempfile
import unittest
from pathlib import Path

from changelogs import _lib
from changelogs import add
from changelogs import area_add


class FakeStdinStream(io.StringIO):

    def __init__(self, value: str = "", tty: bool = False):
        super().__init__(value)
        self._tty = tty

    def isatty(self) -> bool:
        return self._tty


class ChangelogHelpersTest(unittest.TestCase):

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.repo_root = Path(self._tmp.name)
        changelogs = self.repo_root / "changelogs"
        changelogs.mkdir()
        (changelogs / "sections.yaml").write_text("""\
bug_fixes:
  title: Bug fixes
new_features:
  title: New features
""")
        (changelogs / "areas.yaml").write_text("""\
# NB: areas listed here are the canonical set accepted by per-entry changelog
#     filenames in changelogs/current/

build:
  title: build
tls:
  title: tls
""")
        self.environ = {"BUILD_WORKSPACE_DIRECTORY": str(self.repo_root)}

    def tearDown(self):
        self._tmp.cleanup()

    def test_slugify_truncates_on_word_boundaries(self):
        self.assertEqual(_lib.slugify("Fixed something."), "fixed-something")
        self.assertEqual(
            _lib.slugify("Fixed a bug where the HTTP filter per-route configuration regressed."),
            "fixed-a-bug-where-the-http-filter-per",
        )

    def test_add_non_interactive_writes_expected_file(self):
        stdout = io.StringIO()
        exit_code = add.main(
            [
                "--section=bug_fixes",
                "--area=tls",
                "--change=Fixed something.",
            ],
            stdin=FakeStdinStream(),
            stdout=stdout,
            stderr=io.StringIO(),
            environ=self.environ,
        )

        self.assertEqual(exit_code, 0)
        path = self.repo_root / "changelogs/current/bug_fixes/tls__fixed-something.rst"
        self.assertEqual(path.read_text(), "Fixed something.\n")
        self.assertEqual(stdout.getvalue().strip(), "changelogs/current/bug_fixes/tls__fixed-something.rst")

    def test_add_unknown_area_mentions_area_add(self):
        stderr = io.StringIO()
        exit_code = add.main(
            [
                "--section=bug_fixes",
                "--area=unknown_area",
                "--change=Fixed something.",
            ],
            stdin=FakeStdinStream(),
            stdout=io.StringIO(),
            stderr=stderr,
            environ=self.environ,
        )

        self.assertEqual(exit_code, 1)
        self.assertIn("changelog-area-add", stderr.getvalue())

    def test_add_existing_filename_appends_suffix(self):
        existing = self.repo_root / "changelogs/current/bug_fixes/tls__fixed-something.rst"
        existing.parent.mkdir(parents=True, exist_ok=True)
        existing.write_text("Fixed something.\n")

        exit_code = add.main(
            [
                "--section=bug_fixes",
                "--area=tls",
                "--change=Fixed something.",
            ],
            stdin=FakeStdinStream(),
            stdout=io.StringIO(),
            stderr=io.StringIO(),
            environ=self.environ,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(
            (self.repo_root / "changelogs/current/bug_fixes/tls__fixed-something-2.rst").read_text(),
            "Fixed something.\n",
        )

    def test_area_add_inserts_in_sorted_order(self):
        exit_code = area_add.main(
            ["--area=access_log"],
            stdin=FakeStdinStream(),
            stdout=io.StringIO(),
            stderr=io.StringIO(),
            environ=self.environ,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(
            (self.repo_root / "changelogs/areas.yaml").read_text(),
            """\
# NB: areas listed here are the canonical set accepted by per-entry changelog
#     filenames in changelogs/current/

access_log:
  title: access_log
build:
  title: build
tls:
  title: tls
""",
        )

    def test_area_add_refuses_duplicates(self):
        stderr = io.StringIO()
        exit_code = area_add.main(
            ["--area=tls"],
            stdin=FakeStdinStream(),
            stdout=io.StringIO(),
            stderr=stderr,
            environ=self.environ,
        )

        self.assertEqual(exit_code, 1)
        self.assertIn("already exists", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()

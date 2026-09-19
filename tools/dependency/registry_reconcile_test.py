#!/usr/bin/env python3

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import registry_reconcile


class RegistryReconcileTest(unittest.TestCase):
    def test_scan_module_files_collects_unique_pins(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = pathlib.Path(tmpdir)
            first = root / "MODULE.bazel"
            second = root / "mobile.MODULE.bazel"
            first.write_text(
                """
                bazel_dep(name = \"alpha\", version = \"1.0.0\")
                bazel_dep(name = \"envoy\")
                single_version_override(
                    module_name = \"beta\",
                    version = \"2.0.0\",
                )
                """
            )
            second.write_text(
                """
                bazel_dep(name = \"alpha\", version = \"1.0.0\")
                bazel_dep(name = \"gamma\", version = \"3.0.0\")
                """
            )

            scanned = registry_reconcile.scan_module_files([str(first), str(second)])

            self.assertEqual(
                scanned,
                [
                    {
                        "name": "alpha",
                        "versions": ["1.0.0"],
                        "files": [str(first), str(second)],
                    },
                    {"name": "beta", "versions": ["2.0.0"], "files": [str(first)]},
                    {"name": "gamma", "versions": ["3.0.0"], "files": [str(second)]},
                ],
            )

    def test_scan_module_files_tracks_multiple_versions(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = pathlib.Path(tmpdir)
            first = root / "MODULE.bazel"
            second = root / "mobile.MODULE.bazel"
            first.write_text('bazel_dep(name = "alpha", version = "1.0.0")\n')
            second.write_text('bazel_dep(name = "alpha", version = "2.0.0")\n')

            self.assertEqual(
                registry_reconcile.scan_module_files([str(first), str(second)]),
                [
                    {
                        "name": "alpha",
                        "versions": ["1.0.0", "2.0.0"],
                        "files": [str(first), str(second)],
                    }
                ],
            )

    def test_rewrite_module_files_updates_versions_in_place(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = pathlib.Path(tmpdir)
            module_file = root / "MODULE.bazel"
            module_file.write_text(
                """
                bazel_dep(name = \"alpha\", version = \"1.0.0\")
                single_version_override(
                    module_name = \"beta\",
                    version = \"2.0.0\",
                )
                bazel_dep(name = \"gamma\", version = \"3.0.0\")
                """
            )

            changes = registry_reconcile.rewrite_module_files(
                [str(module_file)], {"alpha": "1.1.0", "beta": "2.1.0"}
            )

            self.assertEqual(
                changes,
                [
                    {
                        "name": "alpha",
                        "from": "1.0.0",
                        "from_versions": ["1.0.0"],
                        "to": "1.1.0",
                        "files": [str(module_file)],
                    },
                    {
                        "name": "beta",
                        "from": "2.0.0",
                        "from_versions": ["2.0.0"],
                        "to": "2.1.0",
                        "files": [str(module_file)],
                    },
                ],
            )
            self.assertEqual(
                module_file.read_text(),
                """
                bazel_dep(name = \"alpha\", version = \"1.1.0\")
                single_version_override(
                    module_name = \"beta\",
                    version = \"2.1.0\",
                )
                bazel_dep(name = \"gamma\", version = \"3.0.0\")
                """,
            )

    def test_rewrite_module_files_unifies_multiple_existing_versions(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = pathlib.Path(tmpdir)
            first = root / "MODULE.bazel"
            second = root / "docs.MODULE.bazel"
            first.write_text('bazel_dep(name = "alpha", version = "1.0.0")\n')
            second.write_text('bazel_dep(name = "alpha", version = "2.0.0")\n')

            changes = registry_reconcile.rewrite_module_files(
                [str(first), str(second)], {"alpha": "3.0.0"}
            )

            self.assertEqual(
                changes,
                [
                    {
                        "name": "alpha",
                        "from": "1.0.0, 2.0.0",
                        "from_versions": ["1.0.0", "2.0.0"],
                        "to": "3.0.0",
                        "files": [str(first), str(second)],
                    }
                ],
            )
            self.assertEqual(first.read_text(), 'bazel_dep(name = "alpha", version = "3.0.0")\n')
            self.assertEqual(second.read_text(), 'bazel_dep(name = "alpha", version = "3.0.0")\n')


if __name__ == "__main__":
    unittest.main()

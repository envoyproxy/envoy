"""Tests the Windows module-definition file generator for dynamic module host callbacks."""

import os
import re
import sys
import unittest

import generate_windows_exports_def

_EXPORT_LINE = re.compile(r"^  (envoy_dynamic_module_callback_[a-z0-9_]+)$")


class GenerateWindowsExportsDefTest(unittest.TestCase):

    def test_sorts_and_deduplicates_definitions(self):
        source = ("void envoy_dynamic_module_callback_b(int x) {}\n"
                  "void envoy_dynamic_module_callback_a(int x) {}\n"
                  "void envoy_dynamic_module_callback_a(int x) {}\n")
        expected = ("EXPORTS\n"
                    "  envoy_dynamic_module_callback_a\n"
                    "  envoy_dynamic_module_callback_b\n")
        self.assertEqual(generate_windows_exports_def.generate(source), expected)

    def test_ignores_references_that_are_not_definitions(self):
        source = ("// See envoy_dynamic_module_callback_commented(ctx) for details.\n"
                  'IS_ENVOY_BUG("envoy_dynamic_module_callback_wrapped_"\n'
                  '             "string: not implemented");\n'
                  "  envoy_dynamic_module_callback_called(ctx);\n"
                  "void envoy_dynamic_module_callback_defined(int x) {}\n")
        expected = "EXPORTS\n  envoy_dynamic_module_callback_defined\n"
        self.assertEqual(generate_windows_exports_def.generate(source), expected)

    def test_empty_source_returns_only_the_exports_header(self):
        self.assertEqual(generate_windows_exports_def.generate(""), "EXPORTS\n")

    def test_generated_file_from_real_abi_impl_is_well_formed(self):
        # Validates the file produced from the real abi_impl.cc, guarding against a parsing
        # regression that silently drops callbacks from the Windows export list.
        with open(os.environ["WINDOWS_EXPORTS_DEF"], encoding="utf-8") as def_file:
            lines = def_file.read().splitlines()
        self.assertTrue(lines, "the module-definition file is empty")
        self.assertEqual(lines[0], "EXPORTS")

        names = []
        for line in lines[1:]:
            match = _EXPORT_LINE.match(line)
            self.assertIsNotNone(match, "malformed export line: {}".format(line))
            names.append(match.group(1))

        self.assertIn("envoy_dynamic_module_callback_log", names)
        self.assertIn("envoy_dynamic_module_callback_log_enabled", names)
        self.assertGreater(len(names), 100, "the export list looks truncated")
        self.assertEqual(names, sorted(names), "exports must be sorted")
        self.assertEqual(len(names), len(set(names)), "exports must be unique")


if __name__ == "__main__":
    # Ignore any test-runner arguments (for example the "-l trace --log-path" that coverage builds
    # pass to every test binary) so unittest does not treat them as test names.
    unittest.main(argv=[sys.argv[0]])

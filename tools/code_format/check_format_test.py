#!/usr/bin/env python3

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.code_format.check_format import FormatChecker


class DeprecatedEnvoyRepositoryArgErrorsTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.checker = FormatChecker(["check", "."])

    def deprecated_errors(self, contents, file_path="./foo/BUILD"):
        return self.checker.deprecated_envoy_repository_arg_errors(file_path, contents)

    def test_repository_kwarg_after_comment_reports_correct_line(self):
        contents = """envoy_extension_cc_test(
    name = "tracer_impl_test",
    # TODO(wrowe): envoy_extension_ rules don't currently exclude windows extensions
    repository = "@envoy",
    tags = ["skip_on_windows"],
)
"""
        self.assertEqual(self.deprecated_errors(contents), [
            "./foo/BUILD:4: deprecated Envoy macro `repository` argument is not allowed in in-tree callers"
        ])

    def test_unrelated_repository_kwarg_ignored(self):
        contents = """envoy_package()
oci_push(repository = "envoyproxy/envoy")
"""
        self.assertEqual(self.deprecated_errors(contents), [])

    def test_select_positional_single_line_reports_once(self):
        contents = 'envoy_select_envoy_mobile_xds(["x.cc"], "@envoy")\n'
        self.assertEqual(self.deprecated_errors(contents), [
            "./foo/BUILD:1: deprecated repository string argument is not allowed for envoy_select_* helpers"
        ])

    def test_select_positional_multi_line_reports_once(self):
        contents = """envoy_select_envoy_mobile_xds(
    ["x.cc"],
    "@envoy",
)
"""
        self.assertEqual(self.deprecated_errors(contents), [
            "./foo/BUILD:3: deprecated repository string argument is not allowed for envoy_select_* helpers"
        ])

    def test_select_repository_kwarg_reports_once(self):
        contents = 'envoy_select_envoy_mobile_xds(["x.cc"], repository = "@envoy")\n'
        self.assertEqual(self.deprecated_errors(contents), [
            "./foo/BUILD:1: deprecated Envoy macro `repository` argument is not allowed in in-tree callers"
        ])

    def test_select_force_libcpp_is_not_reported(self):
        contents = 'envoy_select_force_libcpp(["a"], "b")\n'
        self.assertEqual(self.deprecated_errors(contents), [])

    def test_bazel_envoy_files_are_allow_listed(self):
        self.assertTrue(
            self.checker.allow_listed_for_deprecated_envoy_repository_args(
                "./bazel/envoy_select.bzl"))
        self.assertFalse(
            self.checker.allow_listed_for_deprecated_envoy_repository_args("./foo/BUILD"))


if __name__ == "__main__":
    unittest.main()

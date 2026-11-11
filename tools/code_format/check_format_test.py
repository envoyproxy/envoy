#!/usr/bin/env python3
"""Tests for the custom source line checks in check_format.py.

Run with `bazel test //tools/code_format:check_format_test` or directly from the repository root
with `python3 tools/code_format/check_format_test.py`.
"""

import os
import pathlib
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_format  # noqa: E402


class HostLoadUpdateTest(unittest.TestCase):
    """The per-host load report stats must be updated through HostUtility, which also counts a host
    load update on the cluster (ClusterLoadReportStats::upstream_host_load_updates_)."""

    ERROR_PREFIX = "Don't update HostStats::rq_total_, rq_success_ or rq_error_ directly"

    @classmethod
    def setUpClass(cls):
        repo_root = pathlib.Path(__file__).resolve().parents[2]
        cls.checker = check_format.FormatChecker(["check", "--path", str(repo_root)])

    def host_load_update_errors(self, line, file_path="./source/common/router/router.cc"):
        errors = []
        self.checker.check_source_line(line, file_path, errors.append)
        return [error for error in errors if error.startswith(self.ERROR_PREFIX)]

    def test_direct_updates_are_reported(self):
        for line in (
                "  host->stats().rq_success_.inc();",
                "  upstream_host->stats().rq_error_.inc();",
                "  upstream_request.upstreamHost()->stats().rq_error_.inc();",
                "  host_->stats().rq_total_.inc();",
                "  host.stats().rq_total_.add(2);",
        ):
            self.assertEqual(len(self.host_load_update_errors(line)), 1, line)

    def test_other_updates_are_not_reported(self):
        for line in (
                # Not a HostStats counter.
                "  stats_.rq_total_.inc();",
                "  config_->stats().rq_total_.inc();",
                # Not latched by the load stats reporter.
                "  host->stats().rq_active_.dec();",
                "  host->stats().cx_total_.inc();",
                # Reads are fine.
                "  host->stats().rq_success_.value();",
                "  host->stats().rq_success_.latch();",
        ):
            self.assertEqual(self.host_load_update_errors(line), [], line)

    def test_helpers_are_allow_listed(self):
        self.assertEqual(
            self.host_load_update_errors(
                "  host.stats().rq_success_.inc();", "./source/common/upstream/host_utility.cc"),
            [])

    def test_tests_are_not_checked(self):
        self.assertEqual(
            self.host_load_update_errors(
                "  host->stats().rq_success_.inc();",
                "./test/common/upstream/load_stats_reporter_impl_test.cc"), [])

    def test_contrib_is_checked(self):
        self.assertEqual(
            len(
                self.host_load_update_errors(
                    "  host->stats().rq_error_.inc();",
                    "./contrib/some_filter/filters/http/source/filter.cc")), 1)


if __name__ == "__main__":
    unittest.main()

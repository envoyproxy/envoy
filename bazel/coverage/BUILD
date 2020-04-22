load("@rules_python//python:defs.bzl", "py_binary")

py_binary(
    name = "coverage_report_generator",
    srcs = ["coverage_report_generator.py"],
)

filegroup(
    name = "coverage_support",
    srcs = ["collect_cc_coverage.sh"],
)

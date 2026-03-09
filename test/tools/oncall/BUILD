load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_test")

licenses(["notice"])  # Apache 2

py_test(
    name = "oncall_rotation_test",
    srcs = ["oncall_rotation_test.py"],
    data = [
        "//:reviewers.yaml",
        "//tools/oncall:rotation.ical",
        "//tools/oncall:rotation.yaml",
    ],
    deps = [
        "//tools/oncall:oncall_calendar_lib",
        requirement("pyyaml"),
    ],
)

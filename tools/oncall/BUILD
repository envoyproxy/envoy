load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")

licenses(["notice"])  # Apache 2

py_library(
    name = "oncall_calendar_lib",
    srcs = ["oncall_calendar.py"],
    deps = [
        ":gen_ical_lib",
        requirement("pyyaml"),
    ],
)

py_library(
    name = "gen_ical_lib",
    srcs = ["gen_ical_lib.py"],
)

py_binary(
    name = "rotation",
    srcs = ["rotation.py"],
    deps = [
        ":oncall_calendar_lib",
        requirement("aio.run.runner"),
    ],
)

py_test(
    name = "ensure_up_to_date_test",
    srcs = ["ensure_up_to_date_test.py"],
    data = [
        "rotation.ical",
        "rotation.yaml",
        "//:reviewers.yaml",
    ],
    deps = [
        ":oncall_calendar_lib",
        requirement("pyyaml"),
    ],
)

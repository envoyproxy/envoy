load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_binary", "py_library")

licenses(["notice"])  # Apache 2

package(default_visibility = ["//test/tools/oncall:__pkg__"])

exports_files([
    "rotation.yaml",
    "rotation.ical",
])

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

load("@rules_python//python:defs.bzl", "py_binary", "py_test")

licenses(["notice"])  # Apache 2

py_binary(
    name = "proto_breaking_change_detector",
    srcs = ["detector.py"],
    data = [
        ":buf.lock",
        ":buf.yaml",
        "@com_github_bufbuild_buf//:buf",
    ],
    main = "detector.py",
    tags = ["manual"],
    deps = [
        "//tools:run_command",
    ],
)

py_test(
    name = "proto_breaking_change_detector_test",
    srcs = ["detector_test.py"],
    data = [
        "//tools/testdata/api_proto_breaking_change_detector:proto_breaking_change_detector_testdata",
        "@com_github_bufbuild_buf//:buf",
    ],
    main = "detector_test.py",
    python_version = "PY3",
    srcs_version = "PY3",
    tags = ["manual"],
    deps = [
        ":proto_breaking_change_detector",
        "@rules_python//python/runfiles",
    ],
)

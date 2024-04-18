load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")

licenses(["notice"])  # Apache 2

py_binary(
    name = "detector",
    srcs = [
        "detector.py",
    ],
    data = [
        "@com_github_bufbuild_buf//:buf",
        "@envoy_api//:proto_breaking_change_detector_buf_config",
    ],
    main = "detector.py",
    tags = ["manual"],
    deps = [
        ":buf_utils",
        ":detector_errors",
    ],
)

py_binary(
    name = "detector_ci",
    srcs = [
        "detector_ci.py",
    ],
    args = ["$(location @com_github_bufbuild_buf//:buf)"],
    data = [
        "@com_github_bufbuild_buf//:buf",
    ],
    deps = [
        ":detector",
        "@envoy_repo",
    ],
)

py_library(
    name = "buf_utils",
    srcs = [
        "buf_utils.py",
    ],
    deps = [
        ":detector_errors",
        requirement("envoy.base.utils"),
    ],
)

py_library(
    name = "detector_errors",
    srcs = [
        "detector_errors.py",
    ],
)

py_test(
    name = "detector_test",
    srcs = ["detector_test.py"],
    data = [
        "//tools/testdata/api_proto_breaking_change_detector:proto_breaking_change_detector_testdata",
    ],
    main = "detector_test.py",
    tags = ["manual"],
    deps = [
        ":detector",
        "@rules_python//python/runfiles",
    ],
)

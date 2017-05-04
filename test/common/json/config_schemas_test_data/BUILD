licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_py_test_binary",
)

envoy_package()

envoy_py_test_binary(
    name = "generate_test_data",
    srcs = [
        "generate_test_data.py",
        "util.py",
    ] + glob(["test_*.py"]),
)

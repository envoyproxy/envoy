licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_py_test_binary",
)

envoy_package()

exports_files([
    "gen_git_sha.sh",
    "git_sha_rewriter.py",
    "stack_decode.py",
    "check_format.py",
    "header_order.py",
    "envoy_build_fixer.py",
])

envoy_py_test_binary(
    name = "socket_passing",
    srcs = [
        "socket_passing.py",
    ],
)

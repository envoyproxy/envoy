load("@rules_python//python:defs.bzl", "py_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_platform_dep",
    "envoy_package",
    "envoy_py_test_binary",
)

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "gen_git_sha.sh",
    "code_format/check_format.py",
    "code_format/header_order.py",
    "code_format/envoy_build_fixer.py",
    "check_repositories.sh",
])

envoy_py_test_binary(
    name = "socket_passing",
    srcs = [
        "socket_passing.py",
    ],
)

py_library(
    name = "run_command",
    srcs = [
        "run_command.py",
    ],
    visibility = ["//visibility:public"],
)

envoy_cc_binary(
    name = "bootstrap2pb",
    srcs = ["bootstrap2pb.cc"],
    deps = [
        "//source/common/api:api_lib",
        "//source/common/common:assert_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:isolated_store_lib",
        "@envoy_api//envoy/config/bootstrap/v2:pkg_cc_proto",
    ] + envoy_cc_platform_dep("//source/exe:platform_impl_lib"),
)

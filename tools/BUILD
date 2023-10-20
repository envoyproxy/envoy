load("@rules_python//python:defs.bzl", "py_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_package",
    "envoy_py_test_binary",
)
load("//tools/python:namespace.bzl", "envoy_py_namespace")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_namespace()

exports_files([
    "gen_git_sha.sh",
    "check_repositories.sh",
])

py_binary(
    name = "stack_decode",
    srcs = ["stack_decode.py"],
)

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
        "//source/common/common:random_generator_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/exe:platform_impl_lib",
        "//test/test_common:test_version_linkstamp",
        "@envoy_api//envoy/config/bootstrap/v2:pkg_cc_proto",
    ],
)

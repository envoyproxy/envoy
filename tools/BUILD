licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_platform_dep",
    "envoy_package",
    "envoy_py_test_binary",
)

envoy_package()

exports_files([
    "gen_git_sha.sh",
    "check_format.py",
    "header_order.py",
    "envoy_build_fixer.py",
    "check_repositories.sh",
])

envoy_py_test_binary(
    name = "socket_passing",
    srcs = [
        "socket_passing.py",
    ],
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
        "@envoy_api//envoy/config/bootstrap/v2:bootstrap_cc",
    ] + envoy_cc_platform_dep("//source/exe:platform_impl_lib"),
)

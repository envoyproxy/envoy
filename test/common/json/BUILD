package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_test")

filegroup(
    name = "config_schema_test_generator",
    srcs = glob([
        "config_schemas_test_data/*.py",
    ]),
)

envoy_cc_test(
    name = "config_schemas_test",
    srcs = ["config_schemas_test.cc"],
    data = [
        "//test/common/json:config_schema_test_generator",
    ],
    deps = [
        "//source/common/json:config_schemas_lib",
        "//source/common/json:json_loader_lib",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test(
    name = "json_loader_test",
    srcs = ["json_loader_test.cc"],
    deps = ["//source/common/json:json_loader_lib"],
)

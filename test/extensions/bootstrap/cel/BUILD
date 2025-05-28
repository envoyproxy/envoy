load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "cel_bootstrap_test",
    srcs = ["cel_bootstrap_test.cc"],
    extension_names = ["envoy.bootstrap.cel"],
    rbe_pool = "6gig",
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/protobuf:utility_lib",
        "//source/extensions/bootstrap/cel:config",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@com_google_cel_cpp//eval/public/structs:cel_proto_wrapper",
        "@envoy_api//envoy/extensions/filters/common/expr/v3:pkg_cc_proto",
    ],
) 
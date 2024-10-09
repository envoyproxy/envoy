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
    name = "config_test",
    srcs = ["config_test.cc"],
    copts = select({
        "//bazel:windows_x86_64": [],  # TODO: fix the windows ANTLR build
        "//conditions:default": [
            "-DUSE_CEL_PARSER",
        ],
    }),
    extension_names = ["envoy.rate_limit_descriptors.expr"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/protobuf:utility_lib",
        "//source/common/router:router_ratelimit_lib",
        "//source/extensions/rate_limit_descriptors/expr:config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/ratelimit:ratelimit_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/rate_limit_descriptors/expr/v3:pkg_cc_proto",
    ],
)

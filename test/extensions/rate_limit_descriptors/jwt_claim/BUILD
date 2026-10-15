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
    extension_names = ["envoy.rate_limit_descriptors.jwt_claim"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/common:base64_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:router_ratelimit_lib",
        "//source/extensions/rate_limit_descriptors/jwt_claim:config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/ratelimit:ratelimit_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/rate_limit_descriptors/jwt_claim/v3:pkg_cc_proto",
    ],
)

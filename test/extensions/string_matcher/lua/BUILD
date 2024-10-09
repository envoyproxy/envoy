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
    name = "lua_test",
    srcs = ["lua_test.cc"],
    extension_names = ["envoy.string_matcher.lua"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/string_matcher/lua:config",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/string_matcher/lua/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "lua_integration_test",
    srcs = ["lua_integration_test.cc"],
    extension_names = ["envoy.string_matcher.lua"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/string_matcher/lua:config",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/extensions/string_matcher/lua/v3:pkg_cc_proto",
    ],
)

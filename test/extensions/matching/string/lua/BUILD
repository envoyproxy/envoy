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
    extension_names = ["envoy.matching.string.lua"],
    deps = [
        "//source/extensions/matching/string/lua:config",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "lua_integration_test",
    srcs = ["lua_integration_test.cc"],
    extension_names = ["envoy.matching.string.lua"],
    deps = [
        "//source/extensions/matching/string/lua:config",
        "//test/integration:http_integration_lib",
    ],
)

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
    name = "match_config_test",
    srcs = ["match_config_test.cc"],
    extension_names = ["envoy.path.match"],
    deps = [
        "//source/extensions/path/match/pattern_template:match:pattern_template_match_lib",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_extension_cc_test(
    name = "rewrite_config_test",
    srcs = ["rewrite_config_test.cc"],
    extension_names = ["envoy.path.rewrite"],
    deps = [
        "//source/extensions/path/rewrite/pattern_template/:pattern_template_rewrite_lib",
        "//test/mocks/server:factory_context_mocks",
    ],
)

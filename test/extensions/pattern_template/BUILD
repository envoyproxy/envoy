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
    extension_names = ["envoy.pattern_template"],
    deps = [
        "//source/extensions/pattern_template/match:match:pattern_template_match_lib",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_extension_cc_test(
    name = "rewrite_config_test",
    srcs = ["rewrite_config_test.cc"],
    extension_names = ["envoy.pattern_template"],
    deps = [
        "//source/extensions/pattern_template/rewrite:pattern_template_rewrite_lib",
        "//test/mocks/server:factory_context_mocks",
    ],
)

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
    extension_names = ["envoy.url_template"],
    deps = [
        "//source/common/stream_info:filter_state_lib",
        "//source/extensions/pattern_template:config",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_extension_cc_test(
    name = "matcher_test",
    srcs = ["matcher_test.cc"],
    extension_names = ["envoy.url_template"],
    deps = [
        "//source/extensions/pattern_template:pattern_template_matching",
    ],
)

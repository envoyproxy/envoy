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
    extension_names = ["envoy.path.match.uri_template.uri_template_matcher"],
    deps = [
        "//source/extensions/path/match/uri_template:config",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_extension_cc_test(
    name = "library_test",
    srcs = ["library_test.cc"],
    extension_names = ["envoy.path.match.uri_template.uri_template_matcher"],
    deps = [
        "//source/extensions/path/match/uri_template:config",
        "//source/extensions/path/match/uri_template:uri_template_match_lib",
        "//test/mocks/server:factory_context_mocks",
    ],
)

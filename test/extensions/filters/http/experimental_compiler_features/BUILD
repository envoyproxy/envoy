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
    name = "experimental_compiler_features_filter_test",
    srcs = ["filter_test.cc"],
    copts = ["-std=c++20"],
    extension_name = "envoy.filters.http.experimental_compiler_features",
    deps = [
        "//source/extensions/filters/http/experimental_compiler_features:experimental_compiler_features_filter_lib",
        "//test/mocks/http:http_mocks",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    copts = ["-std=c++20"],
    extension_name = "envoy.filters.http.experimental_compiler_features",
    deps = [
        "//source/extensions/filters/http/experimental_compiler_features:config",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/http/experimental_compiler_features/v3:pkg_cc_proto",
    ],
)

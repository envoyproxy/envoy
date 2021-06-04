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
    name = "preserve_case_formatter_test",
    srcs = [
        "preserve_case_formatter_test.cc",
    ],
    extension_name = "envoy.http.stateful_header_formatters.preserve_case",
    deps = [
        "//source/extensions/http/header_formatters/preserve_case:preserve_case_formatter",
    ],
)

envoy_extension_cc_test(
    name = "preserve_case_formatter_integration_test",
    srcs = [
        "preserve_case_formatter_integration_test.cc",
    ],
    extension_name = "envoy.http.stateful_header_formatters.preserve_case",
    # Broken until bazel 5.0.0 fix to shorten resulting paths for SymInitialize() failure
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/http/header_formatters/preserve_case:preserve_case_formatter",
        "//test/integration:http_integration_lib",
    ],
)

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
    name = "transform_test",
    srcs = [
        "transform_test.cc",
    ],
    extension_names = ["envoy.filters.http.transform"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/filters/http/transform:config",
        "//test/mocks/api:api_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/transform/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.transform"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/filters/http/transform:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.filters.http.transform"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/filters/http/transform:config",
        "//test/integration:http_integration_lib",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/strings:str_format",
    ],
)

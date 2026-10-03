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
    name = "header_mutation_test",
    srcs = [
        "header_mutation_test.cc",
    ],
    extension_names = ["envoy.filters.http.header_mutation"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/filters/http/header_mutation:config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.header_mutation"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/filters/http/header_mutation:config",
        "//test/common/formatter:command_extension_lib",
        "//test/mocks/init:init_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_cel_test",
    srcs = ["config_cel_test.cc"],
    extension_names = [
        "envoy.filters.http.header_mutation",
        "envoy.formatter.cel",
    ],
    rbe_pool = "linux_x64_small",
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/header_mutation:config",
        "//source/extensions/formatter/cel:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "header_mutation_integration_test",
    size = "large",
    srcs = ["header_mutation_integration_test.cc"],
    extension_names = ["envoy.filters.http.header_mutation"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/filters/http/header_mutation:config",
        "//test/integration:http_integration_lib",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

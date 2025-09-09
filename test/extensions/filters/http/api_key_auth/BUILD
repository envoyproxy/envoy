load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "api_key_auth_test",
    srcs = ["api_key_auth_test.cc"],
    extension_names = ["envoy.filters.http.api_key_auth"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/api_key_auth:api_key_auth_lib",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/extensions/filters/http/api_key_auth/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.api_key_auth"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/api_key_auth:config",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/extensions/filters/http/api_key_auth/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.filters.http.api_key_auth"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/api_key_auth:config",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/api_key_auth/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "api_key_auth_fuzz_proto",
    srcs = ["api_key_auth_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@envoy_api//envoy/extensions/filters/http/api_key_auth/v3:pkg",
    ],
)

envoy_cc_fuzz_test(
    name = "api_key_auth_fuzz_test",
    srcs = ["api_key_auth_fuzz_test.cc"],
    corpus = "api_key_auth_corpus",
    rbe_pool = "6gig",
    deps = [
        ":api_key_auth_fuzz_proto_cc_proto",
        "//source/common/common:regex_lib",
        "//source/common/router:string_accessor_lib",
        "//source/extensions/filters/http/api_key_auth:api_key_auth_lib",
        "//test/extensions/filters/http/common:mock_lib",
        "//test/extensions/filters/http/common/fuzz:http_filter_fuzzer_lib",
        "//test/fuzz:utility_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/api_key_auth/v3:pkg_cc_proto",
    ],
)

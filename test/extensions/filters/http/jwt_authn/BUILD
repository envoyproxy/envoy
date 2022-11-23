load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_library",
    "envoy_cc_mock",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "test_common_lib",
    hdrs = ["test_common.h"],
)

envoy_cc_mock(
    name = "mock_lib",
    hdrs = ["mock.h"],
    deps = [
        "//source/common/http:message_lib",
        "//source/extensions/filters/http/jwt_authn:authenticator_lib",
        "//source/extensions/filters/http/jwt_authn:verifier_lib",
        "//test/mocks/upstream:cluster_manager_mocks",
    ],
)

envoy_extension_cc_test(
    name = "extractor_test",
    srcs = ["extractor_test.cc"],
    extension_names = ["envoy.filters.http.jwt_authn"],
    deps = [
        "//source/extensions/filters/http/jwt_authn:extractor_lib",
        "//test/extensions/filters/http/jwt_authn:test_common_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_names = ["envoy.filters.http.jwt_authn"],
    deps = [
        ":mock_lib",
        "//source/extensions/filters/http/jwt_authn:filter_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "filter_config_test",
    srcs = ["filter_config_test.cc"],
    extension_names = ["envoy.filters.http.jwt_authn"],
    deps = [
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:filter_state_lib",
        "//source/extensions/filters/http/jwt_authn:config",
        "//test/extensions/filters/http/jwt_authn:test_common_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "jwks_async_fetcher_test",
    srcs = ["jwks_async_fetcher_test.cc"],
    extension_names = ["envoy.filters.http.jwt_authn"],
    deps = [
        "//source/extensions/filters/http/jwt_authn:jwks_async_fetcher_lib",
        "//test/extensions/filters/http/jwt_authn:test_common_lib",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_extension_cc_test(
    name = "filter_factory_test",
    srcs = ["filter_factory_test.cc"],
    extension_names = ["envoy.filters.http.jwt_authn"],
    deps = [
        "//source/extensions/filters/http/jwt_authn:config",
        "//test/extensions/filters/http/jwt_authn:test_common_lib",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "jwks_cache_test",
    srcs = ["jwks_cache_test.cc"],
    extension_names = ["envoy.filters.http.jwt_authn"],
    deps = [
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/filters/http/common:jwks_fetcher_lib",
        "//source/extensions/filters/http/jwt_authn:jwks_cache_lib",
        "//test/extensions/filters/http/jwt_authn:test_common_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "jwt_cache_test",
    srcs = ["jwt_cache_test.cc"],
    extension_names = ["envoy.filters.http.jwt_authn"],
    deps = [
        "//source/extensions/filters/http/jwt_authn:jwt_cache_lib",
        "//test/extensions/filters/http/jwt_authn:test_common_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "authenticator_test",
    srcs = ["authenticator_test.cc"],
    extension_names = ["envoy.filters.http.jwt_authn"],
    deps = [
        ":mock_lib",
        "//source/common/common:base64_lib",
        "//source/extensions/filters/http/common:jwks_fetcher_lib",
        "//source/extensions/filters/http/jwt_authn:authenticator_lib",
        "//source/extensions/filters/http/jwt_authn:filter_config_lib",
        "//source/extensions/filters/http/jwt_authn:matchers_lib",
        "//test/extensions/filters/http/common:mock_lib",
        "//test/extensions/filters/http/jwt_authn:test_common_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "filter_integration_test",
    srcs = ["filter_integration_test.cc"],
    extension_names = ["envoy.filters.http.jwt_authn"],
    shard_count = 6,
    deps = [
        "//source/common/router:string_accessor_lib",
        "//source/extensions/filters/http/jwt_authn:config",
        "//test/config:utility_lib",
        "//test/extensions/filters/http/jwt_authn:test_common_lib",
        "//test/integration:http_protocol_integration_lib",
        "//test/integration/filters:header_to_filter_state_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "matcher_test",
    srcs = ["matcher_test.cc"],
    extension_names = ["envoy.filters.http.jwt_authn"],
    deps = [
        ":mock_lib",
        ":test_common_lib",
        "//source/extensions/filters/http/jwt_authn:matchers_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "group_verifier_test",
    srcs = ["group_verifier_test.cc"],
    extension_names = ["envoy.filters.http.jwt_authn"],
    deps = [
        ":mock_lib",
        ":test_common_lib",
        "//source/extensions/filters/http/jwt_authn:verifier_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "verifier_test",
    srcs = [
        "all_verifier_test.cc",
        "provider_verifier_test.cc",
    ],
    extension_names = ["envoy.filters.http.jwt_authn"],
    deps = [
        ":mock_lib",
        ":test_common_lib",
        "//source/extensions/filters/http/jwt_authn:filter_config_lib",
        "//source/extensions/filters/http/jwt_authn:matchers_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "jwt_authn_fuzz_proto",
    srcs = ["jwt_authn_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg",
    ],
)

envoy_cc_fuzz_test(
    name = "jwt_authn_fuzz_test",
    srcs = ["jwt_authn_fuzz_test.cc"],
    corpus = "jwt_authn_corpus",
    deps = [
        ":jwt_authn_fuzz_proto_cc_proto",
        "//source/common/common:regex_lib",
        "//source/common/router:string_accessor_lib",
        "//source/extensions/filters/http/jwt_authn:filter_lib",
        "//test/extensions/filters/http/common:mock_lib",
        "//test/extensions/filters/http/common/fuzz:http_filter_fuzzer_lib",
        "//test/fuzz:utility_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

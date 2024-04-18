load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "substitution_formatter_fuzz_proto",
    srcs = ["substitution_formatter_fuzz.proto"],
    deps = ["//test/fuzz:common_proto"],
)

envoy_cc_fuzz_test(
    name = "substitution_formatter_fuzz_test",
    srcs = ["substitution_formatter_fuzz_test.cc"],
    corpus = "substitution_formatter_corpus",
    dictionaries = [
        "substitution_formatter_fuzz_test.dict",
        "//test/fuzz:headers.dict",
    ],
    deps = [
        ":substitution_formatter_fuzz_proto_cc_proto",
        "//source/common/formatter:substitution_formatter_lib",
        "//test/fuzz:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "command_extension_lib",
    srcs = ["command_extension.cc"],
    hdrs = ["command_extension.h"],
    deps = [
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_test(
    name = "substitution_formatter_test",
    srcs = ["substitution_formatter_test.cc"],
    deps = [
        ":command_extension_lib",
        "//source/common/common:utility_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/http:header_map_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:address_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:stream_id_provider_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "substitution_format_string_test",
    srcs = ["substitution_format_string_test.cc"],
    deps = [
        ":command_extension_lib",
        "//source/common/formatter:substitution_format_string_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_benchmark_binary(
    name = "substitution_formatter_speed_test",
    srcs = ["substitution_formatter_speed_test.cc"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/http:header_map_lib",
        "//source/common/network:address_lib",
        "//test/common/stream_info:test_util",
        "//test/mocks/http:http_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:printers_lib",
    ],
)

envoy_benchmark_test(
    name = "substitution_formatter_speed_test_benchmark_test",
    benchmark_binary = "substitution_formatter_speed_test",
)

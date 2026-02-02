load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_benchmark_binary",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_package",
    "envoy_proto_descriptor",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "scrubber_test_proto",
    srcs = ["scrubber_test.proto"],
    java = False,
)

envoy_proto_descriptor(
    name = "scrubber_test_proto_descriptor",
    srcs = ["scrubber_test.proto"],
    out = "scrubber_test.descriptor",
    external_deps = [
        "well_known_protos",
    ],
)

envoy_cc_test(
    name = "filter_config_test",
    srcs = ["filter_config_test.cc"],
    data = [
        "//test/config/integration/certs",
        "//test/proto:apikeys_proto_descriptor",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/matcher:matcher_lib",
        "//source/extensions/common/matcher:matcher_lib",
        "//source/extensions/filters/http/proto_api_scrubber:filter_config",
        "//source/extensions/matching/http/cel_input:cel_input_lib",
        "//source/extensions/matching/input_matchers/cel_matcher:config",
        "//test/common/matcher:test_utility_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/proto:apikeys_proto_cc_proto",
        "//test/test_common:environment_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/status",
        "@envoy_api//envoy/extensions/filters/http/proto_api_scrubber/v3:pkg_cc_proto",
        "@xds//xds/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    data = [
        ":scrubber_test_proto_descriptor",
        "//test/proto:apikeys_proto_descriptor",
        "//test/proto:bookstore_proto_descriptor",
    ],
    rbe_pool = "6gig",
    deps = [
        ":scrubber_test_proto_cc_proto",
        "//source/common/grpc:common_lib",
        "//source/common/grpc:status_lib",
        "//source/common/http/matching:inputs_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/filters/http/proto_api_scrubber:filter",
        "//source/extensions/filters/http/proto_api_scrubber:filter_config",
        "//source/extensions/matching/http/cel_input:cel_input_lib",
        "//source/extensions/matching/input_matchers/cel_matcher:config",
        "//test/extensions/filters/http/grpc_field_extraction/message_converter:message_converter_test_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/matcher:matcher_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/proto:apikeys_proto_cc_proto",
        "//test/proto:bookstore_proto_cc_proto",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/strings",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = [
        "integration_test.cc",
    ],
    data = [
        ":scrubber_test_proto_descriptor",
        "//test/proto:apikeys_proto_descriptor",
    ],
    extension_names = ["envoy.filters.http.proto_api_scrubber"],
    rbe_pool = "6gig",
    deps = [
        ":scrubber_test_proto_cc_proto",
        "//source/common/grpc:common_lib",
        "//source/common/http/matching:inputs_lib",
        "//source/common/router:string_accessor_lib",
        "//source/extensions/filters/http/proto_api_scrubber:config",
        "//source/extensions/matching/http/cel_input:cel_input_lib",
        "//source/extensions/matching/input_matchers/cel_matcher:config",
        "//source/extensions/matching/network/common:inputs_lib",
        "//test/extensions/filters/http/grpc_field_extraction/message_converter:message_converter_test_lib",
        "//test/integration:http_protocol_integration_lib",
        "//test/proto:apikeys_proto_cc_proto",
        "//test/test_common:registry_lib",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_cel_cpp//parser",
        "@envoy_api//envoy/extensions/filters/http/proto_api_scrubber/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/matching/common_inputs/network/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_benchmark_binary(
    name = "filter_benchmark_test",
    srcs = ["filter_benchmark_test.cc"],
    data = [
        ":scrubber_test_proto_descriptor",
    ],
    deps = [
        ":scrubber_test_proto_cc_proto",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/grpc:common_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/proto_api_scrubber:filter",
        "//source/extensions/filters/http/proto_api_scrubber:filter_config",
        "//source/extensions/matching/http/cel_input:cel_input_lib",
        "//source/extensions/matching/input_matchers/cel_matcher:config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
        "@com_google_cel_cpp//parser",
        "@envoy_api//envoy/extensions/filters/http/proto_api_scrubber/v3:pkg_cc_proto",
        "@xds//xds/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "filter_fuzz_proto",
    srcs = ["filter_fuzz.proto"],
)

envoy_cc_fuzz_test(
    name = "filter_fuzz_test",
    srcs = ["filter_fuzz_test.cc"],
    corpus = "fuzz_corpus",
    deps = [
        ":filter_fuzz_proto_cc_proto",
        "//source/common/common:assert_lib",
        "//source/common/grpc:common_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/proto_api_scrubber:filter",
        "//source/extensions/filters/http/proto_api_scrubber:filter_config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@xds//xds/type/matcher/v3:pkg_cc_proto",
    ],
)

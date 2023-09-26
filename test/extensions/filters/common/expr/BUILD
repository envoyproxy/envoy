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
    name = "context_test",
    srcs = ["context_test.cc"],
    extension_names = ["envoy.filters.http.rbac"],
    deps = [
        "//source/common/network:filter_state_dst_address_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/clusters/original_dst:original_dst_cluster_lib",
        "//source/extensions/filters/common/expr:cel_state_lib",
        "//source/extensions/filters/common/expr:context_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "evaluator_test",
    srcs = ["evaluator_test.cc"],
    extension_names = ["envoy.filters.http.rbac"],
    deps = [
        "//source/extensions/filters/common/expr:evaluator_lib",
        "//test/test_common:utility_lib",
        "@com_google_cel_cpp//eval/public/structs:cel_proto_wrapper",
    ],
)

envoy_proto_library(
    name = "evaluator_fuzz_proto",
    srcs = ["evaluator_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@com_google_googleapis//google/api/expr/v1alpha1:syntax_proto",
    ],
)

envoy_cc_fuzz_test(
    name = "evaluator_fuzz_test",
    srcs = ["evaluator_fuzz_test.cc"],
    corpus = "evaluator_corpus",
    deps = [
        ":evaluator_fuzz_proto_cc_proto",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "//test/common/stream_info:test_util",
        "//test/fuzz:utility_lib",
        "//test/test_common:utility_lib",
    ],
)

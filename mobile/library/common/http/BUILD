load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "client_lib",
    srcs = ["client.cc"],
    hdrs = ["client.h"],
    external_deps = ["abseil_optional"],
    repository = "@envoy",
    deps = [
        "//library/common/buffer:bridge_fragment_lib",
        "//library/common/data:utility_lib",
        "//library/common/event:provisional_dispatcher_lib",
        "//library/common/extensions/filters/http/local_error:local_error_filter_lib",
        "//library/common/http:header_utility_lib",
        "//library/common/network:synthetic_address_lib",
        "//library/common/thread:lock_guard_lib",
        "//library/common/types:c_types_lib",
        "@envoy//envoy/buffer:buffer_interface",
        "@envoy//envoy/event:deferred_deletable",
        "@envoy//envoy/http:api_listener_interface",
        "@envoy//envoy/http:header_map_interface",
        "@envoy//envoy/stats:stats_interface",
        "@envoy//envoy/stats:stats_macros",
        "@envoy//source/common/buffer:buffer_lib",
        "@envoy//source/common/common:lock_guard_lib",
        "@envoy//source/common/common:minimal_logger_lib",
        "@envoy//source/common/common:random_generator_lib",
        "@envoy//source/common/common:thread_lib",
        "@envoy//source/common/common:thread_synchronizer_lib",
        "@envoy//source/common/http:codec_helper_lib",
        "@envoy//source/common/http:codes_lib",
        "@envoy//source/common/http:headers_lib",
        "@envoy//source/common/http:utility_lib",
    ],
)

envoy_cc_library(
    name = "header_utility_lib",
    srcs = ["header_utility.cc"],
    hdrs = ["header_utility.h"],
    repository = "@envoy",
    deps = [
        "//library/common/data:utility_lib",
        "//library/common/types:c_types_lib",
        "@envoy//envoy/buffer:buffer_interface",
        "@envoy//envoy/http:header_map_interface",
        "@envoy//source/common/http:header_map_lib",
    ],
)

envoy_cc_library(
    name = "internal_headers_lib",
    hdrs = ["headers.h"],
    repository = "@envoy",
    deps = [
        "@envoy//envoy/http:header_map_interface",
        "@envoy//source/common/singleton:const_singleton",
        "@envoy//source/common/singleton:threadsafe_singleton",
    ],
)

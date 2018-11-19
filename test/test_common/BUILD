licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_basic_cc_library",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_basic_cc_library(
    name = "printers_includes",
    hdrs = ["printers.h"],
)

envoy_cc_test_library(
    name = "environment_lib",
    srcs = ["environment.cc"],
    hdrs = ["environment.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":network_utility_lib",
        "//include/envoy/server:options_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:compiler_requirements_lib",
        "//source/common/common:macros",
        "//source/common/common:utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/server:options_lib",
    ],
)

envoy_cc_test_library(
    name = "network_utility_lib",
    srcs = ["network_utility.cc"],
    hdrs = ["network_utility.h"],
    deps = [
        ":utility_lib",
        "//include/envoy/network:filter_interface",
        "//source/common/common:assert_lib",
        "//source/common/network:address_lib",
        "//source/common/network:raw_buffer_socket_lib",
        "//source/common/network:utility_lib",
        "//source/common/runtime:runtime_lib",
    ],
)

envoy_cc_test_library(
    name = "contention_lib",
    srcs = ["contention.cc"],
    hdrs = ["contention.h"],
    deps = [
        "//source/common/common:mutex_tracer_lib",
        "//source/common/common:thread_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_library(
    name = "printers_lib",
    srcs = ["printers.cc"],
    deps = [
        ":printers_includes",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
    ],
)

envoy_cc_test_library(
    name = "registry_lib",
    hdrs = ["registry.h"],
    deps = [
        "//include/envoy/registry",
    ],
)

envoy_cc_test_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    external_deps = [
        "abseil_strings",
    ],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/network:address_interface",
        "//source/common/api:api_lib",
        "//source/common/common:empty_string",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:bootstrap_json_lib",
        "//source/common/filesystem:directory_lib",
        "//source/common/http:header_map_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/common/stats:stats_options_lib",
        "@envoy_api//envoy/config/bootstrap/v2:bootstrap_cc",
    ],
)

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    deps = [
        ":utility_lib",
    ],
)

envoy_cc_test_library(
    name = "logging_lib",
    srcs = ["logging.cc"],
    hdrs = ["logging.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "threadsafe_singleton_injector_lib",
    hdrs = ["threadsafe_singleton_injector.h"],
    deps = [
        "//source/common/singleton:threadsafe_singleton",
    ],
)

envoy_cc_test_library(
    name = "test_time_lib",
    srcs = ["test_time.cc"],
    hdrs = ["test_time.h"],
    deps = [
        ":test_time_system_interface",
        "//source/common/event:real_time_system_lib",
    ],
)

envoy_cc_test_library(
    name = "test_time_system_interface",
    hdrs = ["test_time_system.h"],
    deps = [
        "//include/envoy/event:timer_interface",
    ],
)

envoy_cc_test_library(
    name = "simulated_time_system_lib",
    srcs = ["simulated_time_system.cc"],
    hdrs = ["simulated_time_system.h"],
    deps = [
        ":test_time_system_interface",
        "//source/common/event:real_time_system_lib",
    ],
)

envoy_cc_test(
    name = "simulated_time_system_test",
    srcs = ["simulated_time_system_test.cc"],
    deps = [
        ":simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_library(
    name = "tls_utility_lib",
    srcs = ["tls_utility.cc"],
    hdrs = ["tls_utility.h"],
    external_deps = ["ssl"],
    deps = [
        "//source/common/common:assert_lib",
    ],
)

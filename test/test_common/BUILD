load(
    "//bazel:envoy_build_system.bzl",
    "envoy_basic_cc_library",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_select_signal_trace",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "environment_lib",
    srcs = ["environment.cc"],
    hdrs = ["environment.h"],
    external_deps = [
        "abseil_optional",
        "abseil_symbolize",
        "bazel_runfiles",
    ],
    deps = [
        ":network_utility_lib",
        "//envoy/server:options_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:compiler_requirements_lib",
        "//source/common/common:macros",
        "//source/common/common:utility_lib",
        "//source/common/filesystem:filesystem_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/server:options_lib",
    ] + envoy_select_signal_trace(["//source/common/signal:sigaction_lib"]),
)

envoy_cc_test_library(
    name = "network_utility_lib",
    srcs = ["network_utility.cc"],
    hdrs = ["network_utility.h"],
    deps = [
        ":utility_lib",
        "//envoy/network:filter_interface",
        "//source/common/common:assert_lib",
        "//source/common/network:address_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:raw_buffer_socket_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:utility_lib",
        "//source/common/runtime:runtime_lib",
    ],
)

envoy_cc_test_library(
    name = "real_threads_test_helper_lib",
    srcs = ["real_threads_test_helper.cc"],
    hdrs = ["real_threads_test_helper.h"],
    deps = [
        "utility_lib",
        "//source/common/common:thread_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/thread_local:thread_local_lib",
    ],
)

envoy_cc_test(
    name = "network_utility_test",
    srcs = ["network_utility_test.cc"],
    deps = [
        ":environment_lib",
        ":network_utility_lib",
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
    hdrs = ["printers.h"],
    deps = [
        "//envoy/network:address_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
    ],
)

envoy_cc_test_library(
    name = "registry_lib",
    hdrs = ["registry.h"],
    deps = [
        "//envoy/registry",
    ],
)

envoy_cc_test_library(
    name = "resources_lib",
    hdrs = ["resources.h"],
    deps = ["//source/common/singleton:const_singleton"],
)

envoy_cc_test_library(
    name = "test_random_generator_lib",
    srcs = ["test_random_generator.cc"],
    hdrs = ["test_random_generator.h"],
    deps = [
        "//source/common/common:utility_lib",
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
        ":file_system_for_test_lib",
        ":logging_lib",
        ":printers_lib",
        ":resources_lib",
        ":test_random_generator_lib",
        ":test_time_lib",
        ":thread_factory_for_test_lib",
        "//envoy/buffer:buffer_interface",
        "//envoy/http:codec_interface",
        "//envoy/network:address_interface",
        "//envoy/server/overload:thread_local_overload_state",
        "//source/common/api:api_lib",
        "//source/common/common:empty_string",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:decoded_resource_lib",
        "//source/common/config:opaque_resource_decoder_lib",
        "//source/common/filesystem:directory_lib",
        "//source/common/filesystem:filesystem_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/stats:stats_mocks",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/runtime/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "test_runtime_lib",
    hdrs = ["test_runtime.h"],
    deps = [
        "//source/common/runtime:runtime_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "thread_factory_for_test_lib",
    srcs = ["thread_factory_for_test.cc"],
    hdrs = ["thread_factory_for_test.h"],
    deps = [
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "file_system_for_test_lib",
    srcs = ["file_system_for_test.cc"],
    hdrs = ["file_system_for_test.h"],
    deps = [
        ":test_time_lib",
        "//source/common/common:utility_lib",
        "//source/common/filesystem:filesystem_lib",
    ],
)

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    deps = [
        ":utility_lib",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
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

envoy_cc_library(
    name = "global_lib",
    srcs = ["global.cc"],
    hdrs = ["global.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:thread_lib",
    ],
)

envoy_cc_test(
    name = "global_test",
    srcs = ["global_test.cc"],
    deps = [
        ":global_lib",
    ],
)

envoy_cc_test_library(
    name = "only_one_thread_lib",
    srcs = ["only_one_thread.cc"],
    hdrs = ["only_one_thread.h"],
    deps = [":thread_factory_for_test_lib"],
)

envoy_cc_test_library(
    name = "test_time_lib",
    srcs = ["test_time.cc"],
    hdrs = ["test_time.h"],
    deps = [
        ":global_lib",
        ":test_time_system_interface",
        "//source/common/event:real_time_system_lib",
    ],
)

envoy_cc_test_library(
    name = "test_time_system_interface",
    srcs = ["test_time_system.cc"],
    hdrs = ["test_time_system.h"],
    deps = [
        ":global_lib",
        ":only_one_thread_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//source/common/common:thread_lib",
    ],
)

envoy_cc_test_library(
    name = "simulated_time_system_lib",
    srcs = ["simulated_time_system.cc"],
    hdrs = ["simulated_time_system.h"],
    deps = [
        ":test_random_generator_lib",
        ":test_time_system_interface",
        ":thread_factory_for_test_lib",
        "//source/common/event:event_impl_base_lib",
        "//source/common/event:real_time_system_lib",
        "//source/common/event:timer_lib",
    ],
)

envoy_cc_test_library(
    name = "stats_utility_lib",
    hdrs = ["stats_utility.h"],
    deps = [
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
    ],
)

envoy_cc_test_library(
    name = "status_utility_lib",
    hdrs = ["status_utility.h"],
)

envoy_cc_test(
    name = "status_utility_test",
    srcs = ["status_utility_test.cc"],
    deps = [
        ":status_utility_lib",
    ],
)

envoy_cc_test(
    name = "simulated_time_system_test",
    srcs = ["simulated_time_system_test.cc"],
    deps = [
        ":simulated_time_system_lib",
        ":utility_lib",
        "//source/common/event:libevent_scheduler_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "test_time_system_test",
    srcs = ["test_time_system_test.cc"],
    deps = [
        ":simulated_time_system_lib",
        ":test_time_lib",
        ":utility_lib",
    ],
)

envoy_cc_test_library(
    name = "wasm_lib",
    hdrs = ["wasm_base.h"],
    deps = [
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/common/wasm:wasm_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/wasm/v3:pkg_cc_proto",
    ],
)

envoy_basic_cc_library(
    name = "test_version_linkstamp",
    srcs = ["test_version_linkstamp.cc"],
    alwayslink = 1,
)

envoy_cc_test_library(
    name = "delegating_route_utility_lib",
    hdrs = ["delegating_route_utility.h"],
    deps = [
        "//source/common/router:delegating_route_lib",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "quiche_platform_test",
    srcs = ["quiche_platform_test.cc"],
    external_deps = ["quiche_common_platform"],
    deps = [
        "@com_googlesource_quiche//:quiche_common_platform",
        "@com_googlesource_quiche//:quiche_common_platform_endian",
    ],
)

envoy_cc_test(
    name = "http2_platform_test",
    srcs = ["http2_platform_test.cc"],
    external_deps = ["quiche_http2_platform"],
    deps = [
        "//source/extensions/quic_listeners/quiche/platform:flags_impl_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@com_googlesource_quiche//:http2_test_tools_random",
    ],
)

envoy_cc_test(
    name = "quic_platform_test",
    srcs = select({
        "//bazel:linux": ["quic_platform_test.cc"],
        "//conditions:default": [],
    }),
    copts = ["-Wno-unused-parameter"],
    data = ["//test/extensions/transport_sockets/tls/test_data:certs"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        ":quic_platform_epoll_clock_lib",
        "//source/common/memory:stats_lib",
        "//source/extensions/quic_listeners/quiche/platform:flags_impl_lib",
        "//test/common/buffer:utility_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/extensions/transport_sockets/tls:ssl_test_utils",
        "//test/mocks/api:api_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@com_googlesource_quiche//:epoll_server_lib",
        "@com_googlesource_quiche//:quic_core_buffer_allocator_lib",
        "@com_googlesource_quiche//:quic_core_error_codes_lib",
        "@com_googlesource_quiche//:quic_core_types_lib",
        "@com_googlesource_quiche//:quic_platform_expect_bug",
        "@com_googlesource_quiche//:quic_platform_mem_slice_span",
        "@com_googlesource_quiche//:quic_platform_mem_slice_storage",
        "@com_googlesource_quiche//:quic_platform_mock_log",
        "@com_googlesource_quiche//:quic_platform_port_utils",
        "@com_googlesource_quiche//:quic_platform_sleep",
        "@com_googlesource_quiche//:quic_platform_system_event_loop",
        "@com_googlesource_quiche//:quic_platform_test",
        "@com_googlesource_quiche//:quic_platform_test_output",
        "@com_googlesource_quiche//:quic_platform_thread",
    ],
)

envoy_cc_test(
    name = "spdy_platform_test",
    srcs = ["spdy_platform_test.cc"],
    external_deps = ["quiche_spdy_platform"],
    deps = [
        "//source/extensions/quic_listeners/quiche/platform:flags_impl_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@com_googlesource_quiche//:spdy_platform",
        "@com_googlesource_quiche//:spdy_platform_test_helpers",
    ],
)

envoy_cc_test_library(
    name = "epoll_server_platform_impl_lib",
    hdrs = [
        "epoll_address_test_utils_impl.h",
        "epoll_bug_impl.h",
        "epoll_expect_bug_impl.h",
        "epoll_export_impl.h",
        "epoll_logging_impl.h",
        "epoll_ptr_util_impl.h",
        "epoll_test_impl.h",
        "epoll_thread_impl.h",
        "epoll_time_impl.h",
    ],
    external_deps = ["abseil_time"],
    deps = [
        ":quic_platform_expect_bug_impl_lib",
        ":quic_platform_thread_impl_lib",
        "//include/envoy/network:address_interface",
        "//source/extensions/quic_listeners/quiche/platform:quic_platform_base_impl_lib",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test_library(
    name = "quic_platform_epoll_clock_lib",
    srcs = select({
        "//bazel:linux": ["quic_epoll_clock.cc"],
        "//conditions:default": [],
    }),
    hdrs = select({
        "//bazel:linux": ["quic_epoll_clock.h"],
        "//conditions:default": [],
    }),
    tags = ["nofips"],
    deps = [
        "@com_googlesource_quiche//:quic_core_clock_lib",
        "@com_googlesource_quiche//:quic_platform",
        "@com_googlesource_quiche//:quic_platform_epoll_lib",
    ],
)

envoy_cc_test_library(
    name = "quic_platform_epoll_impl_lib",
    hdrs = ["quic_epoll_impl.h"],
    tags = ["nofips"],
    deps = ["@com_googlesource_quiche//:epoll_server_lib"],
)

envoy_cc_test_library(
    name = "quic_platform_expect_bug_impl_lib",
    hdrs = ["quic_expect_bug_impl.h"],
    tags = ["nofips"],
    deps = [
        "@com_googlesource_quiche//:quic_platform_base",
        "@com_googlesource_quiche//:quic_platform_mock_log",
    ],
)

envoy_cc_test_library(
    name = "quic_platform_mock_log_impl_lib",
    hdrs = ["quic_mock_log_impl.h"],
    tags = ["nofips"],
    deps = ["@com_googlesource_quiche//:quic_platform_base"],
)

envoy_cc_test_library(
    name = "quic_platform_port_utils_impl_lib",
    srcs = ["quic_port_utils_impl.cc"],
    hdrs = ["quic_port_utils_impl.h"],
    tags = ["nofips"],
    deps = [
        "//source/common/network:utility_lib",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test_library(
    name = "quic_platform_test_mem_slice_vector_impl_lib",
    hdrs = ["quic_test_mem_slice_vector_impl.h"],
    tags = ["nofips"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "@com_googlesource_quiche//:quic_platform_mem_slice_span",
    ],
)

envoy_cc_test_library(
    name = "quic_platform_sleep_impl_lib",
    hdrs = ["quic_sleep_impl.h"],
    tags = ["nofips"],
    deps = ["@com_googlesource_quiche//:quic_core_time_lib"],
)

envoy_cc_test_library(
    name = "quic_platform_system_event_loop_impl_lib",
    hdrs = ["quic_system_event_loop_impl.h"],
    tags = ["nofips"],
)

envoy_cc_test_library(
    name = "quic_platform_thread_impl_lib",
    hdrs = ["quic_thread_impl.h"],
    tags = ["nofips"],
    deps = [
        "//include/envoy/thread:thread_interface",
        "//source/common/common:assert_lib",
        "//test/test_common:thread_factory_for_test_lib",
    ],
)

envoy_cc_test_library(
    name = "quic_platform_test_impl_lib",
    hdrs = ["quic_test_impl.h"],
    tags = ["nofips"],
    deps = ["//source/common/common:assert_lib"],
)

envoy_cc_test_library(
    name = "quic_platform_test_output_impl_lib",
    srcs = ["quic_test_output_impl.cc"],
    hdrs = ["quic_test_output_impl.h"],
    tags = ["nofips"],
    deps = [
        "//test/test_common:file_system_for_test_lib",
        "@com_googlesource_quiche//:quic_platform_base",
        "@com_googlesource_quiche//:quiche_common_platform",
    ],
)

envoy_cc_test_library(
    name = "quiche_common_platform_test_impl_lib",
    hdrs = ["quiche_test_impl.h"],
)

envoy_cc_test_library(
    name = "spdy_platform_test_helpers_impl_lib",
    hdrs = ["spdy_test_helpers_impl.h"],
    deps = [
        ":quic_platform_expect_bug_impl_lib",
    ],
)

envoy_cc_test_library(
    name = "spdy_platform_test_impl_lib",
    hdrs = ["spdy_test_impl.h"],
)

envoy_cc_test(
    name = "envoy_quic_clock_test",
    srcs = ["envoy_quic_clock_test.cc"],
    tags = ["nofips"],
    deps = [
        "//source/extensions/quic_listeners/quiche/platform:envoy_quic_clock_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.tracers.skywalking"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/tracers/skywalking:config",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/mocks/server:tracer_factory_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "trace_segment_reporter_test",
    srcs = ["trace_segment_reporter_test.cc"],
    extension_names = ["envoy.tracers.skywalking"],
    rbe_pool = "2core",
    deps = [
        ":skywalking_test_helper",
        "//source/extensions/tracers/skywalking:trace_segment_reporter_lib",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_library(
    name = "skywalking_test_helper_lib",
    hdrs = ["skywalking_test_helper.h"],
)

envoy_extension_cc_test(
    name = "skywalking_test_helper",
    extension_names = ["envoy.tracers.skywalking"],
    rbe_pool = "2core",
    deps = [
        ":skywalking_test_helper_lib",
        "//source/common/common:base64_lib",
        "//source/common/common:hex_lib",
        "//test/test_common:utility_lib",
        "@com_github_skyapm_cpp2sky//source:cpp2sky_data_lib",
    ],
)

envoy_extension_cc_test(
    name = "tracer_test",
    srcs = ["tracer_test.cc"],
    extension_names = ["envoy.tracers.skywalking"],
    rbe_pool = "2core",
    deps = [
        ":skywalking_test_helper",
        "//source/extensions/tracers/skywalking:skywalking_tracer_lib",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "skywalking_tracer_impl_test",
    srcs = ["skywalking_tracer_impl_test.cc"],
    extension_names = ["envoy.tracers.skywalking"],
    rbe_pool = "2core",
    deps = [
        ":skywalking_test_helper",
        "//source/extensions/tracers/skywalking:skywalking_tracer_lib",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)

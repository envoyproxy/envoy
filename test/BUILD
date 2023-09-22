load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_pch_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

# TODO(htuch): remove when we have a solution for https://github.com/bazelbuild/bazel/issues/3510
envoy_cc_test_library(
    name = "dummy_main",
    srcs = ["dummy_main.cc"],
)

envoy_cc_test_library(
    name = "main",
    srcs = [
        "main.cc",
        "test_listener.cc",
        "test_runner.cc",
    ],
    hdrs = [
        "test_listener.h",
        "test_runner.h",
    ],
    deps = [
        "//source/common/common:logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/event:libevent_lib",
        "//source/exe:process_wide_lib",
        "//source/server:backtrace_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:global_lib",
        "//test/test_common:printers_lib",
    ],
)

envoy_cc_test_library(
    name = "test_listener_lib",
    srcs = [
        "test_listener.cc",
    ],
    hdrs = ["test_listener.h"],
    deps = [
        "//source/common/common:logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/runtime:runtime_features_lib",
        "//test/test_common:global_lib",
    ],
)

envoy_pch_library(
    name = "test_pch",
    testonly = True,
    external_deps = [
        "googletest",
        "spdlog",
    ],
    includes = [
        "envoy/config/bootstrap/v3/bootstrap.pb.h",
        "envoy/config/cluster/v3/cluster.pb.h",
        "envoy/config/core/v3/base.pb.h",
        "envoy/config/core/v3/config_source.pb.h",
        "envoy/config/route/v3/route_components.pb.h",
        "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h",
        "envoy/service/discovery/v3/discovery.pb.h",
        "gmock/gmock.h",
        "gtest/gtest.h",
        "spdlog/sinks/android_sink.h",
        "spdlog/spdlog.h",
        "test/mocks/access_log/mocks.h",
        "test/mocks/common.h",
        "test/mocks/event/mocks.h",
        "test/mocks/http/mocks.h",
        "test/mocks/network/mocks.h",
        "test/mocks/runtime/mocks.h",
        "test/mocks/server/factory_context.h",
        "test/mocks/server/instance.h",
        "test/mocks/stats/mocks.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//test/mocks:common_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/stats:stats_mocks",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

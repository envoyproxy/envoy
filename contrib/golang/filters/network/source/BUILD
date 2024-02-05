load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

# Golang L4 network filter.

envoy_contrib_package()

envoy_cc_library(
    name = "golang",
    srcs = ["golang.cc"],
    hdrs = ["golang.h"],
    deps = [
        ":cgo",
        ":upstream",
        "//contrib/golang/common/dso:dso_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/golang/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "upstream",
    srcs = ["upstream.cc"],
    hdrs = [
        "upstream.h",
    ],
    deps = [
        "//contrib/golang/common/dso:dso_lib",
        "//envoy/buffer:buffer_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/tcp:conn_pool_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:thread_local_cluster_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/memory:utils_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:filter_state_dst_address_lib",
        "//source/common/network:utility_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/tcp:conn_pool_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/golang/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = [
        "config.h",
    ],
    deps = [
        ":golang",
        ":upstream",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//envoy/server:lifecycle_notifier_interface",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/golang/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "cgo",
    srcs = ["cgo.cc"],
    hdrs = [
        "golang.h",
        "upstream.h",
    ],
    deps = [
        "//contrib/golang/common/dso:dso_lib",
        "//contrib/golang/common/log:log_lib",
        "//envoy/buffer:buffer_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:thread_local_cluster_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/memory:utils_lib",
        "//source/common/network:connection_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/golang/v3alpha:pkg_cc_proto",
    ],
)

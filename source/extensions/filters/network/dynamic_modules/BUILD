load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "filter_config_lib",
    srcs = ["filter_config.cc"],
    hdrs = ["filter_config.h"],
    deps = [
        "//envoy/access_log:access_log_interface",
        "//envoy/common:random_generator_interface",
        "//envoy/server/overload:load_shed_point_interface",
        "//envoy/stats:stats_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/config:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/dynamic_modules:metric_registry",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = [
        "abi_impl.cc",
        "filter.cc",
    ],
    hdrs = ["filter.h"],
    deps = [
        ":filter_config_lib",
        "//envoy/access_log:access_log_interface",
        "//envoy/event:timer_interface",
        "//envoy/http:async_client_interface",
        "//envoy/http:message_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/registry",
        "//envoy/router:string_accessor_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/http:message_lib",
        "//source/common/network:socket_option_lib",
        "//source/common/network:upstream_socket_options_filter_state_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf",
        "//source/common/router:string_accessor_lib",
        "//source/common/stats:utility_lib",
        "//source/common/stream_info:stream_id_provider_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/stream_info:utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/clusters/common:logical_host_lib",
        "//source/extensions/dynamic_modules:abi_context_accessors_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["factory.cc"],
    hdrs = ["factory.h"],
    deps = [
        ":filter_config_lib",
        ":filter_lib",
        "//source/common/access_log:access_log_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/filters/network/dynamic_modules/v3:pkg_cc_proto",
    ],
)

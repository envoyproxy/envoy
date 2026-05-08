load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":load_balancer",
        "//envoy/common:exception_lib",
        "//envoy/common:optref_lib",
        "//envoy/common:random_generator_interface",
        "//envoy/common:time_interface",
        "//envoy/registry",
        "//envoy/runtime:runtime_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib_header",
        "//source/common/upstream:load_balancer_factory_base_lib",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@envoy_api//envoy/extensions/load_balancing_policies/override_host/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "load_balancer",
    srcs = ["load_balancer.cc"],
    hdrs = ["load_balancer.h"],
    deps = [
        ":override_host_filter_state",
        "//envoy/common:exception_lib",
        "//envoy/common:optref_lib",
        "//envoy/common:random_generator_interface",
        "//envoy/common:time_interface",
        "//envoy/http:header_map_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/config:metadata_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "@abseil-cpp//absl/container:inlined_vector",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/override_host/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "override_host_filter_state",
    hdrs = ["override_host_filter_state.h"],
    deps = [
        "//envoy/stream_info:filter_state_interface",
    ],
)

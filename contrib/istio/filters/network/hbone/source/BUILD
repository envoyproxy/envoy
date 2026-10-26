load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# The paired factories share registration code. Distinct extension targets keep
# the contrib binary's dependency list unique when both names are selected.
envoy_cc_contrib_extension(
    name = "upstream_config",
    deps = [":config"],
)

envoy_cc_contrib_extension(
    name = "pool_config",
    srcs = ["pool.cc"],
    hdrs = ["pool.h"],
    deps = [
        ":hbone_lib",
        "//envoy/router:router_filter_interface",
        "//envoy/router:router_interface",
        "//envoy/tcp:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/extensions/upstreams/http/generic:config",
        "//source/extensions/upstreams/tcp/generic:config",
        "@envoy_api//contrib/envoy/extensions/filters/network/istio_hbone/v3alpha:pkg_cc_proto",
    ],
)

# Selecting the filter (or transport alias) also links the application pool
# factories required by CONNECT wait.
envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":hbone_lib",
        ":pool_config",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/config:utility_lib",
        "//source/common/network:raw_buffer_socket_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/transport_sockets/internal_upstream:config",
        "@envoy_api//contrib/envoy/extensions/filters/network/istio_hbone/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "hbone_lib",
    srcs = ["hbone.cc"],
    hdrs = ["hbone.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/stream_info:bool_accessor_interface",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/network:raw_buffer_socket_lib",
        "//source/common/tcp_proxy",
        "//source/extensions/io_socket/user_space:io_handle_lib",
        "//source/extensions/transport_sockets/internal_upstream",
        "@abseil-cpp//absl/strings",
    ],
)

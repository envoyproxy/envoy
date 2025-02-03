load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "reverse_connection_manager_interface",
    srcs = [
        "reverse_connection_manager.h",
    ],
    deps = [
        "//envoy/network:connection_interface",
    ],
)

envoy_cc_library(
    name = "reverse_connection_lib",
    srcs = [
        "reverse_conn_thread_local_registry.cc",
        "reverse_connection_handler.cc",
        "reverse_connection_manager_impl.cc",
    ],
    deps = [
        ":reverse_connection_includes",
        ":reverse_connection_initiator_lib",
        "//contrib/reverse_connection/reverse_connection_listener_config/source:active_reverse_connection_listener_lib",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:lock_guard_lib",
        "//source/common/common:random_generator_lib",
    ],
)

envoy_cc_library(
    name = "reverse_connection_includes",
    hdrs = [
        "reverse_conn_thread_local_registry.h",
        "reverse_connection_handler.h",
        "reverse_connection_manager_impl.h",
    ],
    deps = [
        ":reverse_connection_manager_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/thread_local:thread_local_object",
    ],
)

envoy_cc_library(
    name = "reverse_connection_initiator_lib",
    srcs = ["reverse_connection_initiator.cc"],
    hdrs = ["reverse_connection_initiator.h"],
    deps = [
        ":reverse_connection_includes",
        "//envoy/network:listener_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:filter_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/reverse_conn/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "reverse_conn_global_registry",
    srcs = [
        "reverse_conn_global_registry.cc",
    ],
    hdrs = [
        "reverse_conn_global_registry.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":reverse_connection_includes",
        ":reverse_connection_lib",
        "//contrib/reverse_connection/reverse_connection_listener_config/source:reverse_connection_listener_config_lib",
        "//envoy/server:bootstrap_extension_config_interface",
        "@envoy_api//contrib/envoy/extensions/bootstrap/reverse_connection/v3alpha:pkg_cc_proto",
        "@envoy_api//contrib/envoy/extensions/reverse_connection/reverse_connection_listener_config/v3alpha:pkg_cc_proto",
    ],
    alwayslink = 1,
)
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "reverse_connection_lib",
    srcs = [
        "reverse_conn_thread_local_registry.cc",
        "reverse_connection_handler_impl.cc",
        "reverse_connection_manager_impl.cc",
    ],
    deps = [
        ":reverse_connection_includes",
        ":reverse_connection_initiator_lib",
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
        "reverse_connection_handler_impl.h",
        "reverse_connection_manager_impl.h",
    ],
    deps = [
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
        "@envoy_api//envoy/extensions/filters/http/reverse_conn/v3:pkg_cc_proto",
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
        "//envoy/server:bootstrap_extension_config_interface",
        "@envoy_api//envoy/extensions/bootstrap/reverse_connection/v3:pkg_cc_proto",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "conn_pool_lib",
    srcs = ["conn_pool.cc"],
    hdrs = ["conn_pool.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/http:codec_client_lib",
        "//source/common/network:connection_impl",
        "//source/common/http/http2:conn_pool_lib",
    ],
)
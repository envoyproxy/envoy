load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# Brpc proxy L7 network filter.

envoy_package()

envoy_cc_library(
    name = "message_interface",
    hdrs = ["message.h"],
    deps = [
    	"//source/common/common:assert_lib",
    	"//source/common/buffer:buffer_lib",
	"@envoy_api//envoy/extensions/filters/network/brpc_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "codec_interface",
    hdrs = ["codec.h"],
    deps = [
        ":message_interface",
    ],
)


envoy_cc_library(
    name = "client_interface",
    hdrs = ["client.h"],
    deps = [
	"//include/envoy/event:dispatcher_interface",
	"//include/envoy/upstream:upstream_interface",
	"//include/envoy/network:connection_interface",
	":message_interface",
    ],
)

envoy_cc_library(
    name = "conn_pool_interface",
    hdrs = ["conn_pool.h"],
    deps = [
	":codec_interface",
	":client_interface",
    ],
)

envoy_cc_library(
    name = "request_manager_interface",
    hdrs = ["request_manager.h"],
    deps = [
	":conn_pool_interface",
    ],
)

envoy_cc_library(
    name = "config_interface",
    hdrs = ["config.h"],
    deps = [
	"//source/extensions/filters/network:well_known_names",
	"//source/extensions/filters/network/common:factory_base_lib",
	"@envoy_api//envoy/extensions/filters/network/brpc_proxy/v3:pkg_cc_proto",
    ],
)


envoy_cc_library(
    name = "client_lib",
    srcs = ["client_impl.cc"],
    hdrs = ["client_impl.h"],
    deps = [
	":client_interface",
	":codec_lib",
	"//include/envoy/upstream:cluster_manager_interface",
	"//include/envoy/thread_local:thread_local_interface",
	"//source/common/common:minimal_logger_lib",
	"//source/common/network:filter_lib",
    ],
)

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec_impl.cc"],
    hdrs = ["codec_impl.h"],
    deps = [
	":codec_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "conn_pool_lib",
    srcs = ["conn_pool_impl.cc"],
    hdrs = ["conn_pool_impl.h"],
    deps = [
	":client_lib",
        ":conn_pool_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/upstream:cluster_manager_interface",
	"//include/envoy/event:dispatcher_interface",
    ],
)

envoy_cc_library(
    name = "request_manager_lib",
    srcs = ["request_manager_impl.cc"],
    hdrs = ["request_manager_impl.h"],
    deps = [
    	":request_manager_interface",
	"//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "proxy_filter_lib",
    srcs = ["proxy_filter.cc"],
    hdrs = ["proxy_filter.h"],
    deps = [
    	":request_manager_interface",
	"//include/envoy/network:connection_interface",
	"//include/envoy/network:filter_interface",
	"//include/envoy/upstream:cluster_manager_interface",
	"//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    # TODO(#9953) clean up.
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
	"//source/extensions/filters/network/brpc_proxy:client_lib",
	"//source/extensions/filters/network/brpc_proxy:codec_lib",
	"//source/extensions/filters/network/brpc_proxy:request_manager_lib",
	"//source/extensions/filters/network/brpc_proxy:conn_pool_lib",
	"//source/extensions/filters/network/brpc_proxy:proxy_filter_lib",
        "@envoy_api//envoy/extensions/filters/network/brpc_proxy/v3:pkg_cc_proto",
    ],
)


load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "header_routing_filter_test",
    srcs = ["header_routing_filter_test.cc"],
    extension_names = ["envoy.filters.udp.session.header_routing"],
    rbe_pool = "6gig",
    deps = [
        "//envoy/network:listener_interface",
        "//envoy/router:string_accessor_interface",
        "//envoy/stream_info:uint32_accessor_interface",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/udp/udp_proxy/session_filters/header_routing:config",
        "//source/extensions/filters/udp/udp_proxy/session_filters/header_routing:filter_lib",
        "//test/extensions/filters/udp/udp_proxy:mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/session/header_routing/v3:pkg_cc_proto",
    ],
)

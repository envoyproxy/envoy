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
    extension_names = ["envoy.filters.network.header_routing"],
    rbe_pool = "6gig",
    deps = [
        "//envoy/router:string_accessor_interface",
        "//envoy/stream_info:uint32_accessor_interface",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/header_routing:config",
        "//source/extensions/filters/network/header_routing:filter_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/network/header_routing/v3:pkg_cc_proto",
    ],
)

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
    name = "ext_authz_test",
    srcs = ["ext_authz_test.cc"],
    extension_names = ["envoy.filters.udp.session.ext_authz"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/udp/udp_proxy/session_filters/ext_authz:config",
        "//source/extensions/filters/udp/udp_proxy/session_filters/ext_authz:ext_authz_lib",
        "//test/extensions/filters/common/ext_authz:ext_authz_mocks",
        "//test/extensions/filters/udp/udp_proxy:mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/session/ext_authz/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "ext_authz_integration_test",
    srcs = ["ext_authz_integration_test.cc"],
    extension_names = ["envoy.filters.udp.session.ext_authz"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/udp/udp_proxy:config",
        "//source/extensions/filters/udp/udp_proxy/session_filters/ext_authz:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration:integration_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/session/ext_authz/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/auth/v3:pkg_cc_proto",
    ],
)

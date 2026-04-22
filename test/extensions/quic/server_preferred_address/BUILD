load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_select_enable_http3",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "datasource_server_preferred_address_test",
    srcs = envoy_select_enable_http3(["datasource_server_preferred_address_test.cc"]),
    extension_names = ["envoy.quic.server_preferred_address.datasource"],
    rbe_pool = "6gig",
    deps = envoy_select_enable_http3([
        "//source/extensions/quic/server_preferred_address:datasource_server_preferred_address_config_lib",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:server_factory_context_mocks",
    ]),
)

envoy_extension_cc_test(
    name = "fixed_server_preferred_address_test",
    srcs = envoy_select_enable_http3(["fixed_server_preferred_address_test.cc"]),
    extension_names = ["envoy.quic.server_preferred_address.fixed"],
    rbe_pool = "6gig",
    deps = envoy_select_enable_http3([
        "//source/extensions/quic/server_preferred_address:fixed_server_preferred_address_config_lib",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:server_factory_context_mocks",
    ]),
)

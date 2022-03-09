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
    name = "client_connection_factory_test",
    srcs = ["client_connection_factory_test.cc"],
    extension_names = ["envoy.bootstrap.internal_listener"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/extensions/bootstrap/internal_listener:client_connection_factory",
        "//source/extensions/io_socket/user_space:io_handle_impl_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
    ],
)

envoy_extension_cc_test(
    name = "internal_listener_registry_test",
    srcs = ["internal_listener_registry_test.cc"],
    extension_names = ["envoy.bootstrap.internal_listener"],
    deps = [
        "//source/extensions/bootstrap/internal_listener:config",
        "//test/mocks/thread_local:thread_local_mocks",
    ],
)

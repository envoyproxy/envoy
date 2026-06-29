load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "bpf_datapath_test",
    srcs = ["bpf_datapath_test.cc"],
    deps = [
        "//envoy/common:base_includes",
        "//source/common/network:address_lib",
        "//source/extensions/network/socket_interface/sockmap:bpf_datapath_lib",
    ],
)

envoy_cc_test(
    name = "io_handle_test",
    srcs = ["io_handle_test.cc"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/common:base_includes",
        "//source/extensions/network/socket_interface/sockmap:io_handle_lib",
        "//test/mocks/api:api_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_extension_cc_test(
    name = "socket_interface_test",
    srcs = ["socket_interface_test.cc"],
    extension_names = ["envoy.extensions.network.socket_interface.sockmap"],
    deps = [
        "//envoy/common:base_includes",
        "//envoy/common:exception_lib",
        "//source/extensions/network/socket_interface/sockmap:config",
        "//source/extensions/network/socket_interface/sockmap:io_handle_lib",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/network/socket_interface/sockmap/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "sockmap_integration_test",
    size = "large",
    srcs = ["sockmap_integration_test.cc"],
    extension_names = ["envoy.extensions.network.socket_interface.sockmap"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/network:socket_interface_lib",
        "//source/extensions/filters/network/echo:config",
        "//source/extensions/network/socket_interface/sockmap:config",
        "//test/integration:integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "sockmap_benchmark",
    srcs = ["sockmap_benchmark.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/network:address_lib",
        "//source/extensions/network/socket_interface/sockmap:bpf_datapath_lib",
        "@benchmark",
    ],
)

envoy_benchmark_test(
    name = "sockmap_benchmark_test",
    benchmark_binary = "sockmap_benchmark",
)

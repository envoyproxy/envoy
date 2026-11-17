load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "starttls_socket_tests",
    srcs = [
        "starttls_socket_test.cc",
    ],
    extension_names = ["envoy.transport_sockets.starttls"],
    rbe_pool = {
        "gcc": {"build": "linux_x64_small"},
        "coverage": {"link": "linux_x64_large"},
        "tsan": {"link": "linux_x64_small"},
    },
    deps = [
        "//source/common/network:transport_socket_options_lib",
        "//source/extensions/transport_sockets/starttls:config",
        "//test/mocks/network:network_mocks",
        "@envoy_api//envoy/extensions/transport_sockets/starttls/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "starttls_integration_proto",
    srcs = [":starttls_integration_test.proto"],
)

envoy_extension_cc_test(
    name = "starttls_integration_test",
    size = "large",
    srcs = [
        "starttls_integration_test.cc",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    extension_names = ["envoy.transport_sockets.starttls"],
    # linux_x64_xlarge: the coverage link peaks at 14862MB,
    # over linux_x64_large (12288MiB) with headroom.
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "cto": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_xlarge",
        },
        "tsan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
        "asan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
    },
    deps = [
        ":starttls_integration_proto_cc_proto",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/extensions/transport_sockets/starttls:config",
        "//test/integration:integration_lib",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/extensions/transport_sockets/raw_buffer/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "upstream_starttls_integration_test",
    size = "large",
    srcs = [
        "upstream_starttls_integration_test.cc",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    extension_names = ["envoy.transport_sockets.starttls"],
    # linux_x64_xlarge: the coverage link peaks at 14733MB,
    # over linux_x64_large (12288MiB) with headroom.
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "cto": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_xlarge",
        },
        "tsan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
        "asan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
    },
    deps = [
        ":starttls_integration_proto_cc_proto",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/transport_sockets/starttls:config",
        "//test/integration:integration_lib",
        "//test/test_common:registry_lib",
    ],
)

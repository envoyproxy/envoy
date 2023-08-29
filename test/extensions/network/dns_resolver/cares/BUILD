load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "dns_impl_test",
    srcs = ["dns_impl_test.cc"],
    # TODO(envoyproxy/windows-dev): Under winsock2 this is behaving unusually for windows, even as
    # 127.0.0.1 and ::1 are explicitly added to `c:\windows\system32\drivers\etc\hosts` ... see:
    # https://gist.github.com/wrowe/24fe5b93b58bb444bce7ecc134905395
    tags = ["fails_on_clang_cl"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/network:address_interface",
        "//envoy/network:dns_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:address_lib",
        "//source/common/network:filter_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/stats:stats_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/network/dns_resolver/cares:config",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

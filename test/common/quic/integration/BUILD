load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
    "envoy_select_enable_http3",
)

licenses(["notice"])  # Apache 2

envoy_package()

# TODO(mattklein123): Use target_compatible_with when we switch to Bazel 4.0 instead of multiple
# selects.
envoy_cc_test(
    name = "quic_protocol_integration_test",
    size = "large",
    srcs = envoy_select_enable_http3([
        "quic_protocol_integration_test.cc",
    ]),
    data = ["//test/config/integration/certs"],
    shard_count = 8,
    tags = [
        "fails_on_clang_cl",
        "fails_on_windows",
        "nofips",
    ],
    deps = envoy_select_enable_http3([
        "//source/common/quic:active_quic_listener_lib",
        "//source/common/quic:client_connection_factory_lib",
        "//source/common/quic:quic_factory_lib",
        "//source/common/quic:quic_transport_socket_factory_lib",
        "//test/integration:protocol_integration_test_lib",
        "//test/integration/filters:pause_filter_for_quic_lib",
    ]),
)

# TODO(mattklein123): Use target_compatible_with when we switch to Bazel 4.0 instead of multiple
# selects.
envoy_cc_test(
    name = "quic_http_integration_test",
    size = "large",
    srcs = envoy_select_enable_http3(["quic_http_integration_test.cc"]),
    data = ["//test/config/integration/certs"],
    shard_count = 6,
    # TODO(envoyproxy/windows-dev): Diagnose failure shown only on clang-cl build, see:
    #   https://gist.github.com/wrowe/a152cb1d12c2f751916122aed39d8517
    # TODO(envoyproxy/windows-dev): Diagnose timeout, why opt build test under Windows GCP RBE
    # takes 10x as long as on linux (>300s vs ~30s). Shards = 2 solves for windows, see:
    #   https://github.com/envoyproxy/envoy/pull/13713/files#r512160087
    # Each of these tests exceeds 20s;
    # QuicHttpIntegrationTests/QuicHttpIntegrationTest.MultipleQuicConnections[With|No]BPF*
    tags = [
        "fails_on_clang_cl",
        "fails_on_windows",
        "nofips",
    ],
    deps = envoy_select_enable_http3([
        "//source/common/quic:client_connection_factory_lib",
        "//source/common/quic:codec_lib",
        "//source/common/quic:envoy_quic_client_connection_lib",
        "//source/common/quic:envoy_quic_client_session_lib",
        "//source/common/quic:envoy_quic_connection_helper_lib",
        "//source/common/quic:envoy_quic_proof_verifier_lib",
        "//source/common/quic:quic_transport_socket_factory_lib",
        "//source/extensions/resource_monitors/injected_resource:config",
        "//test/common/quic:quic_test_utils_for_envoy_lib",
        "//test/common/quic:test_utils_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/overload/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/quic/v3:pkg_cc_proto",
    ]),
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_select_enable_http3",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "pending_proof_source_factory_impl_lib",
    srcs = envoy_select_enable_http3(["pending_proof_source_factory_impl.cc"]),
    hdrs = ["pending_proof_source_factory_impl.h"],
    deps = [
        "//envoy/registry",
        "//source/common/quic:envoy_quic_proof_source_factory_interface",
        "//source/common/quic:envoy_quic_proof_source_lib",
    ],
)

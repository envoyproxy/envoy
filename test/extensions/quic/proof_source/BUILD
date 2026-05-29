load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
    "envoy_select_enable_http3",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "pending_proof_source_proto",
    srcs = ["pending_proof_source.proto"],
)

envoy_cc_test_library(
    name = "pending_proof_source_factory_impl_lib",
    srcs = envoy_select_enable_http3(["pending_proof_source_factory_impl.cc"]),
    hdrs = ["pending_proof_source_factory_impl.h"],
    deps = [
        ":pending_proof_source_proto_cc_proto",
        "//envoy/registry",
        "//source/common/quic:envoy_quic_proof_source_factory_interface",
        "//source/common/quic:envoy_quic_proof_source_lib",
    ],
)

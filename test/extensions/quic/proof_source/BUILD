load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "pending_proof_source_factory_impl_lib",
    srcs = ["pending_proof_source_factory_impl.cc"],
    hdrs = ["pending_proof_source_factory_impl.h"],
    tags = ["nofips"],
    deps = [
        "//envoy/registry",
        "//source/common/quic:envoy_quic_proof_source_factory_interface",
        "//source/common/quic:envoy_quic_proof_source_lib",
    ],
)

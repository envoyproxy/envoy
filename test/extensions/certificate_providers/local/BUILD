load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])

envoy_package()

envoy_cc_test(
    name = "local_certificate_provider_test",
    srcs = ["local_certificate_provider_test.cc"],
    deps = [
        "//source/extensions/certificate_providers/local:local_certificate_minter_lib",
        "//source/extensions/certificate_providers/local:local_certificate_provider_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@envoy_api//envoy/extensions/bootstrap/certificate_providers/local/v3:pkg_cc_proto",
    ],
)

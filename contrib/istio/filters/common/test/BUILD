load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "metadata_object_test",
    srcs = ["metadata_object_test.cc"],
    deps = [
        "//contrib/istio/filters/common/source:metadata_object_lib",
        "//envoy/registry",
    ],
)

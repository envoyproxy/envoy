load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

envoy_package()

sh_test(
    name = "envoy_static_test",
    srcs = ["envoy_static_test.sh"],
    data = ["//source/exe:envoy-static"],
)

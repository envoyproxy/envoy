load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "const_singleton",
    hdrs = ["const_singleton.h"],
)

envoy_cc_library(
    name = "manager_impl_lib",
    srcs = ["manager_impl.cc"],
    hdrs = ["manager_impl.h"],
    deps = [
        "//envoy/registry",
        "//envoy/singleton:manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:non_copyable",
        "//source/common/common:thread_lib",
    ],
)

envoy_cc_library(
    name = "threadsafe_singleton",
    hdrs = ["threadsafe_singleton.h"],
    deps = [
        "//source/common/common:assert_lib",
        "@com_google_absl//absl/base",
    ],
)

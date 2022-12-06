load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "profiler_lib",
    srcs = ["profiler.cc"],
    hdrs = ["profiler.h"],
    tcmalloc_dep = 1,
    deps = [
        "@com_google_absl//absl/status:statusor",
    ],
)

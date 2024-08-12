load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "http_client_lib",
    srcs = ["http_client_impl.cc"],
    hdrs = ["http_client_impl.h"],
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/http/ext_proc",
        "//source/extensions/filters/http/ext_proc:client_base_interface",
    ],
)

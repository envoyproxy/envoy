load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "client_base_interface",
    hdrs = ["client_base.h"],
    tags = ["skip_on_windows"],
    deps = [],
)

envoy_cc_library(
    name = "http_client_lib",
    srcs = ["http_client_impl.cc"],
    hdrs = ["http_client_impl.h"],
    tags = ["skip_on_windows"],
    deps = [
        "client_base_interface",
        "//source/common/common:enum_to_int",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http/ext_proc",
    ],
)

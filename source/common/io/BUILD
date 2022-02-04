load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "io_uring_interface",
    hdrs = [
        "io_uring.h",
    ],
    deps = [
        "//envoy/server:bootstrap_extension_config_interface",
        "//source/common/network:address_lib",
    ],
)

envoy_cc_library(
    name = "io_uring_impl_lib",
    srcs = [
        "io_uring_impl.cc",
    ],
    hdrs = [
        "io_uring_impl.h",
    ],
    external_deps = ["uring"],
    deps = [
        ":io_uring_interface",
        "@envoy_api//envoy/extensions/io/io_uring/v3:pkg_cc_proto",
    ],
)

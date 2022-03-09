load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "allow_listed_routes_lib",
    hdrs = ["allow_listed_routes.h"],
    deps = [
        "//envoy/router:internal_redirect_interface",
        "//envoy/stream_info:filter_state_interface",
        "@envoy_api//envoy/extensions/internal_redirect/allow_listed_routes/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":allow_listed_routes_lib",
        "//envoy/registry",
        "//envoy/router:internal_redirect_interface",
        "@envoy_api//envoy/extensions/internal_redirect/allow_listed_routes/v3:pkg_cc_proto",
    ],
)

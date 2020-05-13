licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "only_allow_safe_cross_scheme_redirect_lib",
    hdrs = ["only_allow_safe_cross_scheme_redirect.h"],
    deps = [
        "//include/envoy/router:internal_redirect_interface",
        "//include/envoy/stream_info:filter_state_interface",
        "//source/extensions/internal_redirect:well_known_names",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream_and_upstream",
    deps = [
        ":only_allow_safe_cross_scheme_redirect_lib",
        "//include/envoy/registry",
        "//include/envoy/router:internal_redirect_interface",
        "//source/extensions/internal_redirect:well_known_names",
        "@envoy_api//envoy/extensions/internal_redirect/only_allow_safe_cross_scheme_redirect/v3:pkg_cc_proto",
    ],
)

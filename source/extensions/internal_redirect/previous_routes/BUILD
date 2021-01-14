load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "previous_routes_lib",
    srcs = ["previous_routes.cc"],
    hdrs = ["previous_routes.h"],
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
    # TODO(#9953) clean up by moving the redirect test to extensions.
    extra_visibility = [
        "//test/integration:__subpackages__",
    ],
    security_posture = "robust_to_untrusted_downstream_and_upstream",
    deps = [
        ":previous_routes_lib",
        "//include/envoy/registry",
        "//include/envoy/router:internal_redirect_interface",
        "//source/extensions/internal_redirect:well_known_names",
        "@envoy_api//envoy/extensions/internal_redirect/previous_routes/v3:pkg_cc_proto",
    ],
)

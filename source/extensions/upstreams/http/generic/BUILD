# placeholder build files for security_posture
# Will be filled in as #11327 lands.
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_extension(
    name = "config",
    srcs = [
    ],
    hdrs = [
    ],
    security_posture = "robust_to_untrusted_downstream",
)

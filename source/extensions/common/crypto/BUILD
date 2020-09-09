load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "utility_lib",
    srcs = [
        "crypto_impl.cc",
        "utility_impl.cc",
    ],
    hdrs = [
        "crypto_impl.h",
        "utility_impl.h",
    ],
    external_deps = [
        "ssl",
    ],
    # Legacy test use. TODO(#9953) clean up.
    extra_visibility = [
        "//test/common/config:__subpackages__",
        "//test/common/crypto:__subpackages__",
    ],
    security_posture = "unknown",
    undocumented = True,
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//source/common/common:assert_lib",
        "//source/common/crypto:utility_lib",
    ],
)

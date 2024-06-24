load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "validation_listener_manager_lib",
    srcs = [
        "validation_listener_manager.cc",
    ],
    hdrs = [
        "validation_listener_manager.h",
    ],
    # any changes to this should be reviewed by mobile maintainers
    # to ensure that listener code doesn't leak back into Envoy Mobile.
    extra_visibility = [
        "//source/server/admin:__subpackages__",
        "//test:__subpackages__",
    ],
    deps = [
        "//source/common/listener_manager:listener_manager_lib",
    ],
)

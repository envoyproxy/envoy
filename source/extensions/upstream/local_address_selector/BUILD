load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "default_local_address_selector",
    srcs = [
        "default_local_address_selector.cc",
    ],
    hdrs = [
        "default_local_address_selector.h",
    ],
    external_deps = ["abseil_optional"],
    # This is core Envoy extension.
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/network:address_interface",
        "//envoy/network:socket_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/network:resolver_lib",
        "//source/common/network:socket_option_factory_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    # This is core Envoy config.
    visibility = ["//visibility:public"],
    deps = [
        ":default_local_address_selector",
        "//envoy/registry",
        "@envoy_api//envoy/extensions/upstream/local_address_selector/v3:pkg_cc_proto",
    ],
)

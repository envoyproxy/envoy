load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "tap_interface",
    hdrs = ["tap.h"],
    deps = [
        ":tap_matcher",
        "//include/envoy/http:header_map_interface",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "tap_config_base",
    srcs = ["tap_config_base.cc"],
    hdrs = ["tap_config_base.h"],
    deps = [
        ":tap_interface",
        ":tap_matcher",
        "//source/common/common:assert_lib",
        "//source/common/common:hex_lib",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "tap_matcher",
    srcs = ["tap_matcher.cc"],
    hdrs = ["tap_matcher.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:matchers_lib",
        "//source/common/http:header_utility_lib",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "admin",
    srcs = ["admin.cc"],
    hdrs = ["admin.h"],
    deps = [
        ":tap_interface",
        "//include/envoy/server:admin_interface",
        "//include/envoy/singleton:manager_interface",
        "//source/common/buffer:buffer_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "extension_config_base",
    srcs = ["extension_config_base.cc"],
    hdrs = ["extension_config_base.h"],
    deps = [
        "//include/envoy/thread_local:thread_local_interface",
        "//source/extensions/common/tap:admin",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/common/tap/v3:pkg_cc_proto",
    ],
)

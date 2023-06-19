load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_select_admin_functionality",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "tap_interface",
    hdrs = ["tap.h"],
    deps = [
        "//envoy/http:header_map_interface",
        "//source/extensions/common/matcher:matcher_lib",
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
        "//source/common/common:assert_lib",
        "//source/common/common:hex_lib",
        "//source/extensions/common/matcher:matcher_lib",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "admin",
    srcs = envoy_select_admin_functionality(["admin.cc"]),
    hdrs = envoy_select_admin_functionality(["admin.h"]),
    deps = [
        ":tap_interface",
        "//envoy/server:admin_interface",
        "//envoy/singleton:manager_interface",
        "//source/common/buffer:buffer_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "extension_config_base",
    srcs = envoy_select_admin_functionality(["extension_config_base.cc"]),
    hdrs = envoy_select_admin_functionality(["extension_config_base.h"]),
    deps = [
        "//envoy/thread_local:thread_local_interface",
        "//source/extensions/common/tap:admin",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/common/tap/v3:pkg_cc_proto",
    ],
)

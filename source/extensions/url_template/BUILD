load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Wildcard & Pattern Matching

envoy_extension_package()

envoy_cc_library(
    name = "url_template_matching",
    srcs = ["url_template_matching.cc"],
    hdrs = ["url_template_matching.h"],
    visibility = [
        "//source/common/router:__subpackages__",
        "//source/extensions/url_template:__subpackages__",
        "//test/extensions/url_template:__subpackages__",
    ],
    deps = [
        ":url_template_matching_internal_cc",
        "//envoy/router:router_url_template_interface",
        "//source/common/http:path_utility_lib",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
        "@envoy_api//envoy/extensions/url_template/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "url_template_matching_internal_cc",
    srcs = ["url_template_matching_internal.cc"],
    hdrs = ["url_template_matching_internal.h"],
    deps = [
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:variant",
        "@com_googlesource_code_re2//:re2",
    ],
)

envoy_cc_test(
    name = "url_template_matching_test",
    srcs = ["url_template_matching_test.cc"],
    deps = [
        ":url_template_matching",
        "//test/test_common:status_utility_lib",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_test(
    name = "url_template_matching_internal_test",
    srcs = ["url_template_matching_internal_test.cc"],
    deps = [
        ":url_template_matching_internal_cc",
        "//test/test_common:status_utility_lib",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":url_template_matching",
        "//envoy/registry",
        "//envoy/router:router_url_template_interface",
        "@envoy_api//envoy/extensions/url_template/v3:pkg_cc_proto",
    ],
)

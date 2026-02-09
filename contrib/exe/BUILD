load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_contrib_package",
)
load("//contrib:all_contrib_extensions.bzl", "SELECTED_CONTRIB_EXTENSIONS")

licenses(["notice"])  # Apache 2

envoy_contrib_package()

alias(
    name = "envoy",
    actual = ":envoy-static",
)

envoy_cc_library(
    name = "contrib_version_suffix_lib",
    srcs = ["version_suffix.cc"],
    alwayslink = 1,
)

envoy_cc_binary(
    name = "envoy-static",
    rbe_pool = "6gig",
    stamped = True,
    visibility = ["//visibility:public"],
    deps = [
        ":contrib_version_suffix_lib",
        "//source/exe:envoy_main_entry_lib",
    ] + SELECTED_CONTRIB_EXTENSIONS,
)

envoy_cc_test(
    name = "contrib_version_test",
    srcs = ["contrib_version_test.cc"],
    deps = [
        ":contrib_version_suffix_lib",
        "//source/common/version:version_lib",
    ],
)

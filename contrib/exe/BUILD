load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_contrib_package",
)
load("//contrib:all_contrib_extensions.bzl", "SELECTED_CONTRIB_EXTENSIONS")

licenses(["notice"])  # Apache 2

envoy_contrib_package()

alias(
    name = "envoy",
    actual = ":envoy-static",
)

envoy_cc_binary(
    name = "envoy-static",
    rbe_pool = "6gig",
    stamped = True,
    visibility = ["//visibility:public"],
    deps = ["//source/exe:envoy_main_entry_lib"] + SELECTED_CONTRIB_EXTENSIONS,
)

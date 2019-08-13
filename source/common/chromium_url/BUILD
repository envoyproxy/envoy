licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "chromium_url",
    srcs = [
        "url_canon.cc",
        "url_canon_internal.cc",
        "url_canon_path.cc",
        "url_canon_stdstring.cc",
    ],
    hdrs = [
        "envoy_shim.h",
        "url_canon.h",
        "url_canon_internal.h",
        "url_canon_stdstring.h",
        "url_parse.h",
        "url_parse_internal.h",
    ],
    deps = ["//source/common/common:assert_lib"],
)

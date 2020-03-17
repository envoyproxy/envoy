licenses(["notice"])  # Apache 2

# QUICHE is Google's implementation of QUIC and related protocols. It is the
# same code used in Chromium and Google's servers, but packaged in a form that
# is intended to be easier to incorporate into third-party projects.
#
# QUICHE code falls into three groups:
# 1. Platform-independent code. Most QUICHE code is in this category.
# 2. APIs and type aliases to platform-dependent code/types, referenced by code
#    in group 1. This group is called the "Platform API".
# 3. Definitions of types declared in group 2. This group is called the
#    "Platform impl", and must be provided by the codebase that embeds QUICHE.
#
# Concretely, header files in group 2 (the Platform API) #include header and
# source files in group 3 (the Platform impl). Unfortunately, QUICHE does not
# yet provide a built-in way to customize this dependency, e.g. to override the
# directory or namespace in which Platform impl types are defined. Hence the
# gross hacks in quiche.genrule_cmd, invoked from here to tweak QUICHE source
# files into a form usable by Envoy.
#
# The mechanics of this will change as QUICHE evolves, supplies its own Bazel
# buildfiles, and provides a built-in way to override platform impl directory
# location. However, the end result (QUICHE files placed under
# quiche/{http2,quic,spdy}/, with the Envoy-specific implementation of the
# QUICHE platform APIs in //source/extensions/quic_listeners/quiche/platform/,
# should remain largely the same.

load("@rules_proto//proto:defs.bzl", "proto_library")
load(":genrule_cmd.bzl", "genrule_cmd")
load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_proto_library",
)

src_files = glob([
    "**/*.h",
    "**/*.c",
    "**/*.cc",
], allow_empty=False)

genrule(
    name = "googleurl_files",
    srcs = src_files,
    outs = ["googleurl/" + f for f in src_files],
    cmd = genrule_cmd("@envoy//bazel/external:googleurl.genrule_cmd"),
)

envoy_cc_library(
    name = "url_lib",
    srcs = [
        "googleurl/url/gurl.cc",
        "googleurl/url/third_party/mozilla/url_parse.cc",
        "googleurl/url/url_canon.cc",
        "googleurl/url/url_canon_etc.cc",
        "googleurl/url/url_canon_filesystemurl.cc",
        "googleurl/url/url_canon_fileurl.cc",
        "googleurl/url/url_canon_host.cc",
        "googleurl/url/url_canon_internal.cc",
        "googleurl/url/url_canon_internal.h",
        "googleurl/url/url_canon_internal_file.h",
        "googleurl/url/url_canon_ip.cc",
        "googleurl/url/url_canon_mailtourl.cc",
        "googleurl/url/url_canon_path.cc",
        "googleurl/url/url_canon_pathurl.cc",
        "googleurl/url/url_canon_query.cc",
        "googleurl/url/url_canon_relative.cc",
        "googleurl/url/url_canon_stdstring.cc",
        "googleurl/url/url_canon_stdurl.cc",
        "googleurl/url/url_constants.cc",
        "googleurl/url/url_idna_icu.cc",
        "googleurl/url/url_parse_file.cc",
        "googleurl/url/url_parse_internal.h",
        "googleurl/url/url_util.cc",
        "googleurl/url/url_util_internal.h",
    ],
    hdrs = [
        "googleurl/url/gurl.h",
        "googleurl/url/url_canon.h",
        "googleurl/url/url_canon_icu.h",
        "googleurl/url/url_canon_ip.h",
        "googleurl/url/url_canon_stdstring.h",
        "googleurl/url/url_constants.h",
        "googleurl/url/url_file.h",
        "googleurl/url/url_util.h",
        "third_party/mozilla/url_parse.h",
    ],
    # linkopts = ["-licuuc"],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        ":base_lib",
        ":base_strings_lib",
    ],
)

envoy_cc_library(
    name = "polyfills_lib",
    hdrs = [
        "googleurl/polyfill/base/base_export.h",
        "googleurl/polyfill/base/component_export.h",
        "googleurl/polyfill/base/debug/alias.h",
        "googleurl/polyfill/base/export_template.h",
        "googleurl/polyfill/base/logging.h",
        "googleurl/polyfill/base/trace_event/memory_usage_estimator.h",
    ],
    repository = "@envoy",
)

envoy_cc_library(
    name = "base_lib",
    hdrs = [
        "googleurl/base/compiler_specific.h",
        "googleurl/base/debug/leak_annotations.h",
        "googleurl/base/macros.h",
        "googleurl/base/no_destructor.h",
        "googleurl/base/optional.h",
        "googleurl/base/stl_util.h",
        "googleurl/base/template_util.h",
    ],
    repository = "@envoy",
    deps = [
        ":build_config_lib",
        ":polyfills_lib",
    ],
)

envoy_cc_library(
    name = "base_strings_lib",
    srcs = [
        "googleurl/base/strings/string16.cc",
        "googleurl/base/strings/string_piece.cc",
        "googleurl/base/strings/string_util.cc",
        "googleurl/base/strings/string_util_constants.cc",
        "googleurl/base/strings/utf_string_conversion_utils.cc",
        "googleurl/base/strings/utf_string_conversions.cc",
    ],
    hdrs = [
        "googleurl/base/strings/char_traits.h",
        "googleurl/base/strings/string16.h",
        "googleurl/base/strings/string_piece.h",
        "googleurl/base/strings/string_piece_forward.h",
        "googleurl/base/strings/string_util.h",
        "googleurl/base/strings/string_util_posix.h",
        "googleurl/base/strings/utf_string_conversion_utils.h",
        "googleurl/base/strings/utf_string_conversions.h",
    ],
    repository = "@envoy",
    deps = [
        ":base_lib",
        ":base_third_party_icu_lib",
        ":build_config_lib",
        ":polyfills_lib",
    ],
)

envoy_cc_library(
    name = "build_config_lib",
    hdrs = ["googleurl/build/build_config.h"],
    repository = "@envoy",
)

envoy_cc_library(
    name = "base_third_party_icu_lib",
    srcs = ["googleurl/base/third_party/icu_utf.cc"],
    hdrs = ["googleurl/base/third_party/icu_utf.h"],
    repository = "@envoy",
)

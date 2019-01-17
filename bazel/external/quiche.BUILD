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
# gross hacks in this file.
#
# Transformations to QUICHE tarball performed here:
# - Move subtree under quiche/ base dir, for clarity in #include statements.
# - Rewrite include directives for platform/impl files.
#
# The mechanics of this will change as QUICHE evolves, supplies its own Bazel
# buildfiles, and provides a built-in way to override platform impl directory
# location. However, the end result (QUICHE files placed under
# quiche/{http2,quic,spdy}/, with the Envoy-specific implementation of the
# QUICHE platform APIs in //source/extensions/quic_listeners/quiche/platform/,
# should remain largely the same.

src_files = glob([
    "**/*.h",
    "**/*.c",
    "**/*.cc",
    "**/*.inc",
    "**/*.proto",
])

# TODO(mpwarres): remove use of sed once QUICHE provides a cleaner way to
#   override platform impl directory location.
genrule(
    name = "quiche_files",
    srcs = src_files,
    outs = ["quiche/" + f for f in src_files],
    cmd = "\n".join(
        ["sed -e '/^#include/ s!net/[^/]*/platform/impl/!extensions/quic_listeners/quiche/platform/!' $(location %s) > $(location :%s)" % (
            f,
            "quiche/" + f,
        ) for f in src_files],
    ),
    visibility = ["//visibility:private"],
)

# Note: in dependencies below that reference Envoy build targets in the main
# repository (particularly for QUICHE platform libs), use '@' instead of
# '@envoy' as the repository identifier. Otherwise, Bazel generates duplicate
# object files for the same build target (one under
# bazel-out/.../bin/external/, and one under bazel-out/.../bin/), eventually
# resulting in link-time errors.

cc_library(
    name = "http2_platform",
    hdrs = [
        "quiche/http2/platform/api/http2_arraysize.h",
        "quiche/http2/platform/api/http2_bug_tracker.h",
        "quiche/http2/platform/api/http2_containers.h",
        "quiche/http2/platform/api/http2_estimate_memory_usage.h",
        "quiche/http2/platform/api/http2_export.h",
        "quiche/http2/platform/api/http2_flag_utils.h",
        "quiche/http2/platform/api/http2_flags.h",
        "quiche/http2/platform/api/http2_macros.h",
        "quiche/http2/platform/api/http2_mock_log.h",
        "quiche/http2/platform/api/http2_optional.h",
        "quiche/http2/platform/api/http2_ptr_util.h",
        "quiche/http2/platform/api/http2_reconstruct_object.h",
        "quiche/http2/platform/api/http2_string.h",
        "quiche/http2/platform/api/http2_string_piece.h",
        "quiche/http2/platform/api/http2_string_utils.h",
        "quiche/http2/platform/api/http2_test_helpers.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@envoy//source/extensions/quic_listeners/quiche/platform:http2_platform_impl_lib",
    ],
)

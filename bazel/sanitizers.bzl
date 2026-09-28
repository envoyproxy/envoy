"""Sanitizer build configuration."""

def sanitizer_deps():
    """Returns sanitizer-specific dependencies based on build configuration.

    For MSAN/TSAN builds, this provides the foreign_cc-built libc++ with
    sanitizer instrumentation. These libraries will be linked in place of
    the system libc++.
    """
    return select({
        Label("//bazel:msan_build"): [Label("//bazel/deps:libcxx_msan_wrapper")],
        Label("//bazel:tsan_build"): [Label("//bazel/deps:libcxx_tsan_wrapper")],
        "//conditions:default": [],
    })

"""Sanitizer build configuration."""

def sanitizer_deps():
    """Returns sanitizer-specific dependencies based on build configuration.

    For MSAN/TSAN builds, this provides the foreign_cc-built libc++ with
    sanitizer instrumentation. These libraries will be linked in place of
    the system libc++.
    """
    return select({
        "@envoy//bazel:msan_build": ["@envoy//bazel/deps:libcxx_msan_wrapper"],
        "@envoy//bazel:tsan_build": ["@envoy//bazel/deps:libcxx_tsan_wrapper"],
        "//conditions:default": [],
    })

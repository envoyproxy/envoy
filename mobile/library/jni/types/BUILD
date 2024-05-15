load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_library(
    name = "jni_exception_lib",
    srcs = [
        "exception.cc",
    ],
    hdrs = [
        "exception.h",
    ],
    repository = "@envoy",
    deps = [
        ":jni_string_lib",
        "//library/jni:jni_helper_lib",
        "@envoy//source/common/common:assert_lib",
    ],
    # We need this to ensure that we link this into the .so even though there are no code references.
    alwayslink = True,
)

envoy_cc_library(
    name = "jni_string_lib",
    hdrs = [
        "string.h",
    ],
    repository = "@envoy",
    deps = [
        "//library/jni:jni_helper_lib",
    ],
    # We need this to ensure that we link this into the .so even though there are no code references.
    alwayslink = True,
)

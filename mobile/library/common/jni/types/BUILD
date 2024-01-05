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
        "//library/common/jni/import:jni_import_lib",
        "//library/common/jni/types:jni_string_lib",
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
        "//library/common/jni/import:jni_import_lib",
        "//library/common/jni/types:jni_env_lib",
    ],
    # We need this to ensure that we link this into the .so even though there are no code references.
    alwayslink = True,
)

envoy_cc_library(
    name = "jni_env_lib",
    srcs = [
        "env.cc",
    ],
    hdrs = [
        "env.h",
    ],
    repository = "@envoy",
    deps = [
        "//library/common/jni:jni_support_lib",
        "//library/common/jni/import:jni_import_lib",
        "//library/common/jni/types:jni_javavm_lib",
        "@envoy//source/common/common:assert_lib",
    ],
    # We need this to ensure that we link this into the .so even though there are no code references.
    alwayslink = True,
)

envoy_cc_library(
    name = "jni_javavm_lib",
    srcs = [
        "java_virtual_machine.cc",
    ],
    hdrs = [
        "java_virtual_machine.h",
    ],
    repository = "@envoy",
    deps = [
        "//library/common/jni:jni_support_lib",
        "//library/common/jni/import:jni_import_lib",
        "@envoy//source/common/common:assert_lib",
    ],
    # We need this to ensure that we link this into the .so even though there are no code references.
    alwayslink = True,
)

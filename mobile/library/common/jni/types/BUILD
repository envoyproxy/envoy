load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_defines", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

cc_library(
    name = "jni_exception_lib",
    srcs = [
        "exception.cc",
    ],
    hdrs = [
        "exception.h",
    ],
    defines = envoy_mobile_defines("@envoy"),
    deps = [
        "//library/common/jni/import:jni_import_lib",
        "//library/common/jni/types:jni_string_lib",
        "@envoy//source/common/common:assert_lib",
    ],
    # We need this to ensure that we link this into the .so even though there are no code references.
    alwayslink = True,
)

cc_library(
    name = "jni_string_lib",
    hdrs = [
        "string.h",
    ],
    defines = envoy_mobile_defines("@envoy"),
    deps = [
        "//library/common/jni/import:jni_import_lib",
        "//library/common/jni/types:jni_env_lib",
    ],
    # We need this to ensure that we link this into the .so even though there are no code references.
    alwayslink = True,
)

cc_library(
    name = "jni_env_lib",
    srcs = [
        "env.cc",
    ],
    hdrs = [
        "env.h",
    ],
    defines = envoy_mobile_defines("@envoy"),
    deps = [
        "//library/common/jni:jni_support_lib",
        "//library/common/jni/import:jni_import_lib",
        "//library/common/jni/types:jni_javavm_lib",
        "@envoy//source/common/common:assert_lib",
    ],
    # We need this to ensure that we link this into the .so even though there are no code references.
    alwayslink = True,
)

cc_library(
    name = "jni_javavm_lib",
    srcs = [
        "java_virtual_machine.cc",
    ],
    hdrs = [
        "java_virtual_machine.h",
    ],
    defines = envoy_mobile_defines("@envoy"),
    deps = [
        "//library/common/jni:jni_support_lib",
        "//library/common/jni/import:jni_import_lib",
        "@envoy//source/common/common:assert_lib",
    ],
    # We need this to ensure that we link this into the .so even though there are no code references.
    alwayslink = True,
)

load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_defines", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

cc_library(
    name = "jni_env_lib",
    srcs = [
        "Env.cc",
    ],
    hdrs = [
        "Env.h",
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
        "JavaVirtualMachine.cc",
    ],
    hdrs = [
        "JavaVirtualMachine.h",
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

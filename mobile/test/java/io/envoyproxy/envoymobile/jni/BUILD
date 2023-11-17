load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_android_test")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_mobile_android_test(
    name = "jni_helper_test",
    srcs = [
        "JniHelperTest.java",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_helper_test.so",
    ],
    native_lib_name = "envoy_jni_helper_test",
)

envoy_mobile_android_test(
    name = "jni_utility_test",
    srcs = [
        "JniUtilityTest.java",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_utility_test.so",
    ],
    native_lib_name = "envoy_jni_utility_test",
    deps = [
        "@maven//:com_google_protobuf_protobuf_javalite",
    ],
)

load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_android_test")

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

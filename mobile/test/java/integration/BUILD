load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_android_test")

envoy_mobile_android_test(
    name = "android_engine_start_test",
    srcs = [
        "AndroidEnvoyEngineStartUpTest.java",
    ],
    native_deps = [
        "//library/common/jni:ndk_envoy_lib.so",
        "//library/common/jni:ndk_envoy_lib.jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
    ],
)

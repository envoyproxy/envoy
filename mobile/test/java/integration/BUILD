load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_android_test")

envoy_mobile_android_test(
    name = "android_engine_start_test",
    srcs = [
        "AndroidEnvoyEngineStartUpTest.java",
    ],
    native_deps = [
        "//library/common/jni:libndk_envoy_jni.so",
        "//library/common/jni:libndk_envoy_jni.jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
    ],
)

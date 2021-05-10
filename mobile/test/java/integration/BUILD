load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_android_test")

licenses(["notice"])  # Apache 2

envoy_package()

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

envoy_mobile_android_test(
    name = "android_engine_flow_test",
    srcs = [
        "AndroidEnvoyFlowTest.java",
    ],
    native_deps = [
        "//library/common/jni:libndk_envoy_jni.so",
        "//library/common/jni:libndk_envoy_jni.jnilib",
    ],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
    ],
)

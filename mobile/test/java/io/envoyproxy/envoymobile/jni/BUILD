load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:envoy_mobile_android_test.bzl", "envoy_mobile_android_test")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_mobile_android_test(
    name = "jni_helper_test",
    srcs = [
        "JniHelperTest.java",
    ],
    javacopts = ["-Xep:JUnit4TestNotRun:OFF"],
    native_deps = [
        "//test/jni:libenvoy_jni_helper_test.so",
    ],
    native_lib_name = "envoy_jni_helper_test",
    test_class = "io.envoyproxy.envoymobile.jni.JniHelperTest",
)

envoy_mobile_android_test(
    name = "jni_utility_test",
    srcs = [
        "JniUtilityTest.java",
    ],
    native_deps = [
        "//test/jni:libenvoy_jni_utility_test.so",
    ],
    native_lib_name = "envoy_jni_utility_test",
    test_class = "io.envoyproxy.envoymobile.jni.JniUtilityTest",
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine/types:envoy_c_types_lib",
        "@maven//:com_google_protobuf_protobuf_javalite",
    ],
)

load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_android_test")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_mobile_android_test(
    name = "certificate_verification_tests",
    srcs = [
        "CertificateVerificationTest.java",
    ],
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
        "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
    ],
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/utilities",
        "//library/java/org/chromium/net",
    ],
)

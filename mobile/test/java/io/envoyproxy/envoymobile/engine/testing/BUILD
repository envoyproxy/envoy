load("@build_bazel_rules_android//android:rules.bzl", "android_library")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_android_test")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

android_library(
    name = "testing",
    testonly = True,
    srcs = [
        "RequestScenario.java",
        "Response.java",
        "TestJni.java",
    ],
    visibility = ["//test:__subpackages__"],
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
    ],
)

envoy_mobile_android_test(
    name = "quic_test_server_test",
    srcs = [
        "QuicTestServerTest.java",
    ],
    exec_properties = {
        # TODO(lfpino): Remove this once the sandboxNetwork=off works for ipv4 localhost addresses.
        "sandboxNetwork": "standard",
    },
    native_deps = [
        "//test/common/jni:libenvoy_jni_with_test_extensions.so",
    ] + select({
        "@platforms//os:macos": [
            "//test/common/jni:libenvoy_jni_with_test_extensions_jnilib",
        ],
        "//conditions:default": [],
    }),
    native_lib_name = "envoy_jni_with_test_extensions",
    deps = [
        ":testing",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
    ],
)

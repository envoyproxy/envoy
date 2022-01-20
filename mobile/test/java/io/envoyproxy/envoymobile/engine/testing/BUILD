load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("@envoy_mobile//bazel:kotlin_test.bzl", "envoy_mobile_android_test")

licenses(["notice"])  # Apache 2

envoy_package()

android_library(
    name = "testing",
    testonly = True,
    srcs = [
        "QuicTestServer.java",
    ],
    visibility = ["//test:__subpackages__"],
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
    library_path = "test/common/jni",
    native_deps = [
        "//test/common/jni:libquic_test_server_jni.so",
        "//test/common/jni:libquic_test_server_jni.jnilib",
    ],
    deps = [
        ":testing",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
    ],
)

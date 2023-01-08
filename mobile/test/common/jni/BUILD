load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("//bazel:kotlin_lib.bzl", "envoy_mobile_so_to_jni_lib")

licenses(["notice"])  # Apache 2

envoy_package()

# OS X binary (.jnilib) for Quic Test Server
envoy_mobile_so_to_jni_lib(
    name = "libquic_test_server_jni_jnilib",
    testonly = True,
    native_dep = "libquic_test_server_jni.so",
)

# Binary for Quic Test Server
cc_binary(
    name = "libquic_test_server_jni.so",
    testonly = True,
    srcs = [
        "quic_test_server_jni_interface.cc",
        "@bazel_tools//tools/jdk:current_java_runtime",
    ],
    copts = ["-std=c++17"],
    linkshared = True,
    deps = [
        "//library/common/jni:envoy_jni_with_test_extensions_lib",
        "//test/common/integration:quic_test_server_interface_lib",
    ],
)

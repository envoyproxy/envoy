load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("//bazel:kotlin_lib.bzl", "envoy_mobile_so_to_jni_lib")
load("//bazel:envoy_mobile_test_extensions.bzl", "TEST_EXTENSIONS")

licenses(["notice"])  # Apache 2

envoy_package()

# Library which contains all the JNI related targets and test extensions
cc_library(
    name = "envoy_jni_with_test_extensions_lib",
    deps = [
        "//library/common/jni:envoy_jni_lib",
    ] + TEST_EXTENSIONS,
)

# Base binary (.so) for testing
cc_binary(
    name = "libenvoy_jni_with_test_extensions.so",
    linkshared = True,
    deps = [
        ":envoy_jni_with_test_extensions_lib",
        "@envoy_mobile_extra_jni_deps//:extra_jni_dep",
    ],
)

## Targets for local execution
# macOS binary (.jnilib) for testing
envoy_mobile_so_to_jni_lib(
    name = "libenvoy_jni_with_test_extensions_jnilib",
    native_dep = "libenvoy_jni_with_test_extensions.so",
)

# Binary for Quic Test Server
cc_binary(
    name = "libquic_test_server_jni.so",
    testonly = True,
    srcs = [
        "quic_test_server_jni_interface.cc",
    ],
    linkshared = True,
    deps = [
        "//test/common/integration:quic_test_server_interface_lib",
        "//test/common/jni:envoy_jni_with_test_extensions_lib",
    ],
)

# macOS binary (.jnilib) for Quic Test Server
envoy_mobile_so_to_jni_lib(
    name = "libquic_test_server_jni_jnilib",
    testonly = True,
    native_dep = "libquic_test_server_jni.so",
)

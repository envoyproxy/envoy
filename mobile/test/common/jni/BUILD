load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("//bazel:kotlin_lib.bzl", "envoy_mobile_so_to_jni_lib")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

# Library which contains all the JNI related targets and test extensions
cc_library(
    name = "envoy_jni_with_test_extensions_lib",
    testonly = True,
    deps = [
        ":server_envoy_jni_lib",
        "//library/common/jni:envoy_jni_lib",
        "@envoy_build_config//:test_extensions",
    ],
)

# Library which contains all the JNI related targets.
cc_library(
    name = "server_envoy_jni_lib",
    testonly = True,
    srcs = [
        "test_jni_impl.cc",
    ],
    linkopts = select({
        "@envoy//bazel:dbg_build": ["-Wl,--build-id=sha1"],
        "//conditions:default": [],
    }),
    deps = [
        "//library/common/jni:envoy_jni_lib",
        "//test/common/integration:test_server_interface_lib",
        "//test/common/integration:xds_test_server_interface_lib",
    ],
    # We need this to ensure that we link this into the .so even though there are no code references.
    alwayslink = True,
)

# Base binary (.so) for testing
cc_binary(
    name = "libenvoy_jni_with_test_extensions.so",
    testonly = True,
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
    testonly = True,
    native_dep = "libenvoy_jni_with_test_extensions.so",
)

# Same as above but with listener extensions too.
# This hard-codes the listener manager lib rather than doing a select, so that the kotlin
# proxy tests can run in builds which select out the listener for E-M.
cc_library(
    name = "envoy_jni_with_test_and_listener_extensions_lib",
    testonly = True,
    deps = [
        ":server_envoy_jni_lib",
        "//library/common/jni:envoy_jni_lib",
        "@envoy//source/common/listener_manager:listener_manager_lib",
    ],
)

cc_binary(
    name = "libenvoy_jni_with_test_and_listener_extensions.so",
    testonly = True,
    linkshared = True,
    deps = [
        ":envoy_jni_with_test_and_listener_extensions_lib",
        "@envoy_mobile_extra_jni_deps//:extra_jni_dep",
    ],
)

envoy_mobile_so_to_jni_lib(
    name = "libenvoy_jni_with_test_and_listener_extensions_jnilib",
    testonly = True,
    native_dep = "libenvoy_jni_with_test_and_listener_extensions.so",
)

cc_library(
    name = "jni_helper_test_lib",
    srcs = [
        "jni_helper_test.cc",
    ],
    deps = [
        "//library/common/jni:jni_helper_lib",
    ],
    alwayslink = True,
)

cc_binary(
    name = "libenvoy_jni_helper_test.so",
    testonly = True,
    linkshared = True,
    deps = [
        ":jni_helper_test_lib",
    ],
)

cc_library(
    name = "jni_utility_test_lib",
    srcs = [
        "jni_utility_test.cc",
    ],
    deps = [
        "//library/common/jni:jni_utility_lib",
    ],
    alwayslink = True,
)

cc_binary(
    name = "libenvoy_jni_utility_test.so",
    testonly = True,
    linkshared = True,
    deps = [
        ":jni_utility_test_lib",
    ],
)

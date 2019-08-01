licenses(["notice"])  # Apache 2

load("//bazel:kotlin_lib.bzl", "envoy_mobile_android_library", "envoy_mobile_java_library")

envoy_mobile_android_library(
    name = "envoy_engine_lib",
    srcs = [
        "AndroidEngine.java",
    ],
    custom_package = "io.envoyproxy.envoymobile.engine",
    manifest = "AndroidEngineManifest.xml",
    visibility = ["//visibility:public"],
    deps = [
        "//library/common:envoy_jni_interface_lib",
        "//library/java/src/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/src/io/envoyproxy/envoymobile/engine/types:envoy_c_types_lib",
    ],
)

envoy_mobile_java_library(
    name = "envoy_base_engine_lib",
    srcs = [
        "Engine.java",
        "EnvoyEngine.java",
        "JniLibrary.java",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//library/common:envoy_jni_interface_lib",
        "//library/java/src/io/envoyproxy/envoymobile/engine/types:envoy_c_types_lib",
    ],
)

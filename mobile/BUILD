licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application", "ios_framework", "ios_static_framework")
load("@io_bazel_rules_kotlin//kotlin/internal:toolchains.bzl", "define_kt_toolchain")

envoy_package()

load("//bazel:aar_with_jni.bzl", "aar_with_jni")

ios_static_framework(
    name = "ios_framework",
    hdrs = ["//library/common:main_interface.h"],
    bundle_name = "Envoy",
    minimum_os_version = "10.0",
    visibility = ["//visibility:public"],
    deps = ["//library/common:envoy_main_interface_lib"],
)

genrule(
    name = "ios_dist",
    srcs = ["//:ios_framework"],
    outs = ["ios_out"],
    cmd = """
unzip -o $< -d dist/
touch $@
""",
    stamp = True,
)

aar_with_jni(
    name = "android_aar",
    android_library = "android_lib",
    archive_name = "envoy",
    visibility = ["//visibility:public"],
)

android_library(
    name = "android_lib",
    srcs = ["library/java/io/envoyproxy/envoymobile/Envoy.java"],
    custom_package = "io.envoyproxy.envoymobile",
    manifest = "library/EnvoyManifest.xml",
    deps = ["//library/common:envoy_jni_interface_lib"],
)

genrule(
    name = "android_dist",
    srcs = ["//:android_aar"],
    outs = ["android_out"],
    cmd = """
cp $< dist/envoy.aar
chmod 755 dist/envoy.aar
touch $@
""",
    stamp = True,
)

define_kt_toolchain(
    name = "kotlin_toolchain",
    jvm_target = "1.6",
)

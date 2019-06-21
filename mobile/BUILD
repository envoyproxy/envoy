licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application", "ios_framework", "ios_static_framework")
load("@io_bazel_rules_kotlin//kotlin/internal:toolchains.bzl", "define_kt_toolchain")
load("@io_bazel_rules_kotlin//kotlin:kotlin.bzl", "kt_android_library")

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
    name = "envoy_engine_lib",
    srcs = ["library/java/io/envoyproxy/envoymobile/EnvoyEngine.java"],
    custom_package = "io.envoyproxy.envoymobile",
    manifest = "library/EnvoyManifest.xml",
    deps = ["//library/common:envoy_jni_interface_lib"],
)

# Work around for transitive dependencies related to not including cc_libraries for kt_jvm_library
# Related to: https://github.com/bazelbuild/rules_kotlin/issues/132
#
# Android library drops exported dependencies from dependent rules. The kt_android_library
#  internally is just a macro which wraps two rules into one:
#  https://github.com/bazelbuild/rules_kotlin/blob/326661e7e705d14e754abc2765837aa61bddf205/kotlin/internal/jvm/android.bzl#L28.
#  This causes the sources to be exported and dropped due to it being a transitive dependency.
#  To get around this, we have to redeclare the sources from envoy_engine_lib here in order to be pulled into the
#  kotlin jar.
kt_android_library(
    name = "android_lib",
    srcs = [
        "library/java/io/envoyproxy/envoymobile/EnvoyEngine.java",
        "library/kotlin/io/envoyproxy/envoymobile/Envoy.kt",
    ],
    custom_package = "io.envoyproxy.envoymobile",
    manifest = "library/EnvoyManifest.xml",
    deps = ["envoy_engine_lib"],
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

load("@io_bazel_rules_kotlin//kotlin:kotlin.bzl", "kt_android_library", "kt_jvm_library")

# Android library drops exported dependencies from dependent rules. The kt_android_library
#  internally is just a macro which wraps two rules into one:
#  https://github.com/bazelbuild/rules_kotlin/blob/326661e7e705d14e754abc2765837aa61bddf205/kotlin/internal/jvm/android.bzl#L28.
#  This causes the sources to be exported and dropped due to it being a transitive dependency.
#  To get around this, we have to redeclare the sources from envoy_engine_lib here in order to be pulled into the
#  kotlin jar.
def envoy_mobile_kt_aar_android_library(name, custom_package, manifest, visibility = None, srcs = [], deps = []):
    filegroups = []
    for dep in deps:
        filegroups.append(dep + "_srcs")

    kt_android_library(
        name = name,
        srcs = srcs + filegroups,
        custom_package = custom_package,
        manifest = manifest,
        visibility = ["//visibility:public"],
        deps = deps,
    )

def envoy_mobile_android_library(name, custom_package, manifest, visibility = None, srcs = [], deps = []):
    # These source files must be re-exported to the kotlin custom library rule to ensure their
    # inclusion.
    native.filegroup(
        name = name + "_srcs",
        srcs = srcs,
        visibility = visibility,
    )

    native.android_library(
        name = name,
        srcs = srcs,
        custom_package = custom_package,
        manifest = manifest,
        visibility = visibility,
        deps = deps,
    )

def envoy_mobile_java_library(name, visibility = None, srcs = [], deps = []):
    # These source files must be re-exported to the kotlin custom library rule to ensure their
    # inclusion.
    native.filegroup(
        name = name + "_srcs",
        srcs = srcs,
        visibility = visibility,
    )

    native.java_library(
        name = name,
        srcs = srcs,
        visibility = visibility,
        deps = deps,
    )

def envoy_mobile_kt_library(name, visibility = None, srcs = [], deps = []):
    # These source files must be re-exported to the kotlin custom library rule to ensure their
    # inclusion.
    native.filegroup(
        name = name + "_srcs",
        srcs = srcs,
        visibility = visibility,
    )

    kt_jvm_library(
        name = name,
        srcs = srcs,
        deps = deps,
        visibility = visibility,
    )

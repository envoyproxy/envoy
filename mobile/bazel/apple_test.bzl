load("@build_bazel_rules_apple//apple:ios.bzl", "ios_unit_test")
load("@build_bazel_rules_swift//swift:swift.bzl", "swift_library")
load("@rules_cc//cc:defs.bzl", "objc_library")

# Macro providing a way to easily/consistently define Swift unit test targets.
#
# - Prevents consumers from having to define both swift_library and ios_unit_test targets
# - Provides a set of linker options that is required to properly run tests
# - Sets default visibility and OS requirements
#
# Usage example:
# load("@envoy_mobile//bazel:apple_test.bzl", "envoy_mobile_swift_test")
#
# envoy_mobile_swift_test(
#     name = "sample_test",
#     srcs = [
#         "SampleTest.swift",
#     ],
# )
#
def envoy_mobile_swift_test(name, srcs, data = [], deps = [], repository = ""):
    test_lib_name = name + "_lib"
    swift_library(
        name = test_lib_name,
        srcs = srcs,
        data = data,
        deps = [
            repository + "//library/swift:ios_lib",
        ] + deps,
        linkopts = ["-lresolv.9"],
        visibility = ["//visibility:private"],
    )

    ios_unit_test(
        name = name,
        data = data,
        deps = [test_lib_name],
        minimum_os_version = "11.0",
    )

def envoy_mobile_objc_test(name, srcs, data = [], deps = []):
    test_lib_name = name + "_lib"
    objc_library(
        name = test_lib_name,
        srcs = srcs,
        data = data,
        deps = deps,
        visibility = ["//visibility:private"],
    )

    ios_unit_test(
        name = name,
        data = data,
        deps = [test_lib_name],
        minimum_os_version = "11.0",
    )

load("@build_bazel_rules_apple//apple:ios.bzl", "ios_unit_test")
load("@build_bazel_rules_swift//swift:swift.bzl", "swift_library")

# Macro providing a way to easily/consistently define Swift unit test targets.
#
# - Prevents consumers from having to define both swift_library and ios_unit_test targets
# - Provides a set of linker options that is required to properly run tests
# - Sets default visibility and OS requirements
#
# Usage example:
# load("//bazel:swift_test.bzl", "envoy_mobile_swift_test")
#
# envoy_mobile_swift_test(
#     name = "sample_test",
#     srcs = [
#         "SampleTest.swift",
#     ],
# )
#
def envoy_mobile_swift_test(name, srcs, deps = []):
    test_lib_name = name + "_lib"
    swift_library(
        name = test_lib_name,
        srcs = srcs,
        deps = [
            "//library/swift/src:ios_framework_archive",
        ] + deps,
        linkopts = ["-lresolv.9"],
        visibility = ["//visibility:private"],
    )

    ios_unit_test(
        name = name,
        deps = [test_lib_name],
        minimum_os_version = "10.0",
    )

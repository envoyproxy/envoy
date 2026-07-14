load("@build_bazel_rules_apple//apple:ios.bzl", "ios_unit_test")
load("@build_bazel_rules_swift//swift:swift.bzl", "swift_library")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_defines")
load("@rules_cc//cc:objc_library.bzl", "objc_library")
load("//bazel:config.bzl", "MINIMUM_IOS_VERSION")

def envoy_objc_library(name, hdrs = [], visibility = [], data = [], deps = [], module_name = None, sdk_frameworks = [], srcs = [], testonly = False):
    objc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        copts = ["-ObjC++", "-std=c++20", "-Wno-shorten-64-to-32"],
        defines = envoy_mobile_defines("@envoy"),
        module_name = module_name,
        sdk_frameworks = sdk_frameworks,
        visibility = visibility,
        data = data,
        deps = deps,
        testonly = testonly,
    )

# Macros providing a way to easily/consistently define Swift/ObjC unit test targets.
#
# - Prevents consumers from having to define both swift_library and ios_unit_test targets
# - Provides a set of linker options that is required to properly run tests
# - Sets default visibility and OS requirements
#
# Usage example:
# load("@envoy_mobile//bazel:apple.bzl", "envoy_mobile_swift_test")
#
# envoy_mobile_swift_test(
#     name = "sample_test",
#     srcs = [
#         "SampleTest.swift",
#     ],
# )
#
def envoy_mobile_swift_test(name, srcs, size = None, data = [], deps = [], tags = [], repository = "", visibility = [], flaky = False, exec_properties = {}):
    test_lib_name = name + "_lib"
    swift_library(
        name = test_lib_name,
        srcs = srcs,
        data = data,
        deps = [
            repository + "//library/swift:ios_lib",
        ] + deps,
        linkopts = ["-lresolv.9"],
        testonly = True,
        visibility = ["//visibility:private"],
    )

    ios_unit_test(
        name = name,
        data = data,
        deps = [test_lib_name],
        minimum_os_version = MINIMUM_IOS_VERSION,
        size = size,
        tags = tags,
        visibility = visibility,
        flaky = flaky,
        exec_properties = exec_properties,
    )

def envoy_mobile_objc_test(name, srcs, data = [], deps = [], tags = [], visibility = [], flaky = False):
    test_lib_name = name + "_lib"
    envoy_objc_library(
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
        minimum_os_version = MINIMUM_IOS_VERSION,
        tags = tags,
        visibility = visibility,
        flaky = flaky,
    )

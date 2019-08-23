licenses(["notice"])  # Apache 2

load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application")
load("@build_bazel_rules_swift//swift:swift.bzl", "swift_library")

objc_library(
    name = "appmain",
    srcs = glob([
        "*.h",
        "*.m",
    ]),
    deps = ["//dist:envoy_mobile_ios"],
)

# This is forces linking of necessary Swift dependencies.
# https://github.com/bazelbuild/rules_apple/issues/557
swift_library(
    name = "empty_swift_lib",
    srcs = ["Empty.swift"],
    module_name = "empty_swift_lib",
    deps = [],
)

ios_application(
    name = "app",
    bundle_id = "io.envoyproxy.envoymobile.helloworld",
    families = ["iphone"],
    infoplists = ["Info.plist"],
    minimum_os_version = "10.0",
    deps = [
        "appmain",
        "empty_swift_lib",
    ],
)

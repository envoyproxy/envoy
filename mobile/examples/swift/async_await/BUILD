load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application")
load("@build_bazel_rules_swift//swift:swift.bzl", "swift_library")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

swift_library(
    name = "appmain",
    srcs = glob(["*.swift"]),
    deps = ["//library/swift:ios_lib"],
)

ios_application(
    name = "app",
    bundle_id = "io.envoyproxy.envoymobile.asyncawait",
    families = ["iphone"],
    infoplists = ["Info.plist"],
    minimum_os_version = "15.0",
    provisioning_profile = select({
        "@envoy//bazel:ios_arm64": "//examples:ios_provisioning_profile",
        "//conditions:default": None,
    }),
    visibility = ["//visibility:public"],
    deps = ["appmain"],
)

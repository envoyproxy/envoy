load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("//bazel:config.bzl", "MINIMUM_IOS_VERSION")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

objc_library(
    name = "appmain",
    srcs = glob([
        "*.h",
        "*.m",
    ]),
    deps = ["//:envoy_mobile_ios"],
)

ios_application(
    name = "app",
    bundle_id = "io.envoyproxy.envoymobile.helloworld",
    families = ["iphone"],
    infoplists = ["Info.plist"],
    minimum_os_version = MINIMUM_IOS_VERSION,
    provisioning_profile = select({
        "@envoy//bazel:ios_arm64": "//examples:ios_provisioning_profile",
        "//conditions:default": None,
    }),
    visibility = ["//visibility:public"],
    deps = ["appmain"],
)

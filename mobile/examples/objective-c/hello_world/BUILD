load("//bazel:config.bzl", "MINIMUM_IOS_VERSION")
load("@rules_cc//cc:defs.bzl", "objc_library")
load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application")

licenses(["notice"])  # Apache 2

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
    visibility = ["//visibility:public"],
    deps = ["appmain"],
)

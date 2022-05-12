load("//bazel:config.bzl", "MINIMUM_IOS_VERSION")
load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application")
load("@build_bazel_rules_swift//swift:swift.bzl", "swift_library")

licenses(["notice"])  # Apache 2

swift_library(
    name = "appmain",
    srcs = glob(["*.swift"]),
    deps = ["//library/swift:ios_lib"],
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

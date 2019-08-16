licenses(["notice"])  # Apache 2

load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application")
load("@build_bazel_rules_swift//swift:swift.bzl", "swift_library")

swift_library(
    name = "appmain",
    srcs = glob(["*.swift"]),
    deps = ["//dist:envoy_mobile_ios"],
)

ios_application(
    name = "app",
    bundle_id = "io.envoyproxy.envoymobile.helloworld",
    families = ["iphone"],
    infoplists = ["Info.plist"],
    minimum_os_version = "10.0",
    deps = ["appmain"],
)

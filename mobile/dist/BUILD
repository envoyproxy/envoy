licenses(["notice"])  # Apache 2

load("@build_bazel_rules_apple//apple:apple.bzl", "apple_static_framework_import")

# NOTE: You must first build the top-level targets //:ios_dist and //:android_dist to use the
# artifacts referenced here.
# You can also download the distributables referenced here from envoy-mobile's releases page:
# https://github.com/lyft/envoy-mobile/releases

aar_import(
    name = "envoy_mobile_android",
    aar = "envoy.aar",
    visibility = ["//visibility:public"],
)

apple_static_framework_import(
    name = "envoy_mobile_ios",
    framework_imports = glob(["Envoy.framework/**"]),
    sdk_dylibs = [
        "resolv.9",
        "c++",
    ],
    sdk_frameworks = [
        "SystemConfiguration",
    ],
    visibility = ["//visibility:public"],
)

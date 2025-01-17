load("@build_bazel_rules_apple//apple:apple.bzl", "local_provisioning_profile")
load(
    "@com_github_buildbuddy_io_rules_xcodeproj//xcodeproj:defs.bzl",
    "xcode_provisioning_profile",
)
load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

# Change to your Apple Developer Team ID as shown in
# https://developer.apple.com/account/#!/membership
TEAM_ID = "X4ST43AL9W"

# If needed, change to the name shown by clicking the (i) next to
# "Xcode Managed Profile" in the "Signing and Capabilities" tab
PROFILE_NAME = "iOS Team Provisioning Profile: *"

xcode_provisioning_profile(
    name = "ios_provisioning_profile",
    managed_by_xcode = True,
    provisioning_profile = ":xcode_managed_profile",
    tags = ["manual"],
    visibility = ["//visibility:public"],
)

local_provisioning_profile(
    name = "xcode_managed_profile",
    profile_name = PROFILE_NAME,
    tags = ["manual"],
    team_id = TEAM_ID,
)

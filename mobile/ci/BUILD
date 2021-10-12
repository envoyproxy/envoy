licenses(["notice"])  # Apache 2

xcode_version(
    name = "xcode_13_0",
    default_ios_sdk_version = "15.0",
    default_macos_sdk_version = "11.3",
    default_tvos_sdk_version = "15.0",
    default_watchos_sdk_version = "7.4",
    version = "13.0",
)

available_xcodes(
    name = "local_xcodes",
    default = ":xcode_13_0",
    versions = [
        ":xcode_13_0",
    ],
)

available_xcodes(
    name = "remote_xcodes",
    default = ":xcode_13_0",
    versions = [
        ":xcode_13_0",
    ],
)

xcode_config(
    name = "xcode_config",
    local_versions = ":local_xcodes",
    remote_versions = ":remote_xcodes",
)

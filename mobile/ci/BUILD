licenses(["notice"])  # Apache 2

xcode_version(
    name = "xcode_12_5",
    default_ios_sdk_version = "14.5",
    default_macos_sdk_version = "11.3",
    default_tvos_sdk_version = "14.5",
    default_watchos_sdk_version = "7.4",
    version = "12.5",
)

available_xcodes(
    name = "local_xcodes",
    default = ":xcode_12_5",
    versions = [
        ":xcode_12_5",
    ],
)

available_xcodes(
    name = "remote_xcodes",
    default = ":xcode_12_5",
    versions = [
        ":xcode_12_5",
    ],
)

xcode_config(
    name = "xcode_config",
    local_versions = ":local_xcodes",
    remote_versions = ":remote_xcodes",
)

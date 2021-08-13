licenses(["notice"])  # Apache 2

xcode_version(
    name = "xcode_12_4",
    default_ios_sdk_version = "14.4",
    default_macos_sdk_version = "11.1",
    default_tvos_sdk_version = "14.3",
    default_watchos_sdk_version = "7.2",
    version = "12.4",
)

available_xcodes(
    name = "local_xcodes",
    default = ":xcode_12_4",
    versions = [
        ":xcode_12_4",
    ],
)

available_xcodes(
    name = "remote_xcodes",
    default = ":xcode_12_4",
    versions = [
        ":xcode_12_4",
    ],
)

xcode_config(
    name = "xcode_config",
    local_versions = ":local_xcodes",
    remote_versions = ":remote_xcodes",
)

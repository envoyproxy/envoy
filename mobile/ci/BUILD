licenses(["notice"])  # Apache 2

xcode_version(
    name = "xcode_13_4_0",
    default_ios_sdk_version = "15.5",
    default_macos_sdk_version = "12.3",
    default_tvos_sdk_version = "15.4",
    default_watchos_sdk_version = "8.5",
    version = "13.4.0",
)

available_xcodes(
    name = "local_xcodes",
    default = ":xcode_13_4_0",
    versions = [
        ":xcode_13_4_0",
    ],
)

available_xcodes(
    name = "remote_xcodes",
    default = ":xcode_13_4_0",
    versions = [
        ":xcode_13_4_0",
    ],
)

xcode_config(
    name = "xcode_config",
    local_versions = ":local_xcodes",
    remote_versions = ":remote_xcodes",
)

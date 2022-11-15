licenses(["notice"])  # Apache 2

xcode_version(
    name = "xcode_14_1_0",
    default_ios_sdk_version = "16.1",
    default_macos_sdk_version = "13.0",
    default_tvos_sdk_version = "16.1",
    default_watchos_sdk_version = "9.1",
    version = "14.1",
)

available_xcodes(
    name = "local_xcodes",
    default = ":xcode_14_1_0",
    versions = [
        ":xcode_14_1_0",
    ],
)

available_xcodes(
    name = "remote_xcodes",
    default = ":xcode_14_1_0",
    versions = [
        ":xcode_14_1_0",
    ],
)

xcode_config(
    name = "xcode_config",
    local_versions = ":local_xcodes",
    remote_versions = ":remote_xcodes",
)

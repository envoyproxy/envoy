licenses(["notice"])  # Apache 2

xcode_version(
    name = "xcode_16_1_0",
    default_ios_sdk_version = "18.1",
    default_macos_sdk_version = "15.1",
    default_tvos_sdk_version = "18.1",
    default_watchos_sdk_version = "11.1",
    version = "16.1",
)

available_xcodes(
    name = "local_xcodes",
    default = ":xcode_16_1_0",
    versions = [
        ":xcode_16_1_0",
    ],
)

available_xcodes(
    name = "remote_xcodes",
    default = ":xcode_16_1_0",
    versions = [
        ":xcode_16_1_0",
    ],
)

xcode_config(
    name = "xcode_config",
    local_versions = ":local_xcodes",
    remote_versions = ":remote_xcodes",
)

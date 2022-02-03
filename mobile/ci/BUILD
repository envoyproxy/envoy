licenses(["notice"])  # Apache 2

xcode_version(
    name = "xcode_13_2_1",
    default_ios_sdk_version = "15.2",
    default_macos_sdk_version = "12.1",
    default_tvos_sdk_version = "15.2",
    default_watchos_sdk_version = "8.3",
    version = "13.2.1",
)

available_xcodes(
    name = "local_xcodes",
    default = ":xcode_13_2_1",
    versions = [
        ":xcode_13_2_1",
    ],
)

available_xcodes(
    name = "remote_xcodes",
    default = ":xcode_13_2_1",
    versions = [
        ":xcode_13_2_1",
    ],
)

xcode_config(
    name = "xcode_config",
    local_versions = ":local_xcodes",
    remote_versions = ":remote_xcodes",
)

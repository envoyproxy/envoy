load(
    "@envoy_build_config//:extensions_build_config.bzl",
    "ADDITIONAL_VISIBILITY",
)

licenses(["notice"])  # Apache 2

exports_files([
    "VERSION",
    ".clang-format",
])

# These two definitions exist to help reduce Envoy upstream core code depending on extensions.
# To avoid visibility problems, one can extend ADDITIONAL_VISIBILITY in source/extensions/extensions_build_config.bzl
#
# TODO(#9953) //test/config_test:__pkg__ should probably be split up and removed.
# TODO(#9953) the config fuzz tests should be moved somewhere local and //test/config_test and //test/server removed.
package_group(
    name = "extension_config",
    packages = [
        "//source/exe",
        "//source/extensions/...",
        "//test/config_test",
        "//test/extensions/...",
        "//test/server",
        "//test/server/config_validation",
    ] + ADDITIONAL_VISIBILITY,
)

package_group(
    name = "extension_library",
    packages = [
        "//source/extensions/...",
        "//test/extensions/...",
    ] + ADDITIONAL_VISIBILITY,
)

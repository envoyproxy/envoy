load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/python:namespace.bzl", "envoy_py_namespace")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_namespace()

exports_files([
    "VERSION.txt",
    "API_VERSION.txt",
    ".clang-format",
    "pytest.ini",
    ".coveragerc",
    "CODEOWNERS",
    "OWNERS.md",
    ".github/workflows/_env.yml",
])

alias(
    name = "envoy",
    actual = "//source/exe:envoy",
)

alias(
    name = "envoy.stripped",
    actual = "//source/exe:envoy-static.stripped",
)

filegroup(
    name = "clang_tidy_config",
    srcs = [".clang-tidy"],
    visibility = ["//visibility:public"],
)

# These two definitions exist to help reduce Envoy upstream core code depending on extensions.
# To avoid visibility problems, see notes in source/extensions/extensions_build_config.bzl
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
        "//test/tools/...",
        "//tools/extensions/...",
    ],
)

package_group(
    name = "extension_library",
    packages = [
        "//source/extensions/...",
        "//test/extensions/...",
    ],
)

package_group(
    name = "contrib_library",
    packages = [
        "//contrib/...",
    ],
)

package_group(
    name = "examples_library",
    packages = [
        "//examples/...",
    ],
)

package_group(
    name = "mobile_library",
    packages = [
        "//mobile/...",
    ],
)

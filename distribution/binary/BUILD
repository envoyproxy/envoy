load("@bazel_skylib//rules:common_settings.bzl", "bool_flag", "string_flag")
load("@envoy_toolshed//tarball:compressed_tar.bzl", "compressed_tar")
load("@rules_pkg//pkg:mappings.bzl", "pkg_files")
load("//distribution:packages.bzl", "PUBLISH_PACKAGING_TAGS")
load("//distribution/binary:compiler.bzl", "bundled")

licenses(["notice"])  # Apache 2

# An execution platform with arm64 workers, for the bundled targets of an arm64 build to be built
# on (see `compiler.bzl`).
string_flag(
    name = "arm64-exec-platform",
    build_setting_default = "",
    visibility = ["//visibility:public"],
)

# Strip the release binaries on the host (`strip.bzl`), rather than where they are built: with
# `:arm64-exec-platform`, that is not somewhere the host can run the strip of.
bool_flag(
    name = "host-strip",
    build_setting_default = False,
    visibility = ["//visibility:public"],
)

config_setting(
    name = "host_strip",
    flag_values = {":host-strip": "true"},
    visibility = ["//visibility:public"],
)

bundled(
    name = "envoy",
    mode = "opt",
    stripopts = ["--strip-all"],
    targets = {
        "//distribution:envoy-binary-stripped": "envoy",
        "//distribution:envoy-contrib-binary-stripped": "envoy-contrib",
        "//distribution:envoy-binary": "dbg/envoy",
        "//distribution:envoy-dwarf-joined": "dbg/envoy.dwp",
        "//distribution:envoy-contrib-binary": "dbg/envoy-contrib",
        "//distribution:envoy-contrib-dwarf-joined": "dbg/envoy-contrib.dwp",
        "@com_github_ncopa_suexec//:su-exec": "utils/su-exec",
    },
)

pkg_files(
    name = "release_files",
    srcs = [":envoy"],
    strip_prefix = "envoy/opt",
)

pkg_files(
    name = "debug_files",
    srcs = [":envoy-debug"],
    prefix = "debug",
)

# Written by bsdtar (several GB with the debug info), and compressed with all the cores.
compressed_tar(
    name = "release",
    srcs = [
        ":release_files",
    ],
    out = "release.tar.zst",
    compression = "zstd",
    tags = PUBLISH_PACKAGING_TAGS,
    visibility = ["//distribution/publish:__pkg__"],
)

# The stripped release binaries only (what is signed, see `//distribution:release_binaries`), for
# `ci/do_ci.sh release.multiarch`, which publishes no release tarball.
bundled(
    name = "envoy-stripped",
    mode = "opt",
    stripopts = ["--strip-all"],
    targets = {
        "//distribution:envoy-binary-stripped": "envoy",
        "//distribution:envoy-contrib-binary-stripped": "envoy-contrib",
    },
)

pkg_files(
    name = "release_stripped_files",
    srcs = [":envoy-stripped"],
    strip_prefix = "envoy-stripped/opt",
)

compressed_tar(
    name = "release_stripped",
    srcs = [
        ":release_stripped_files",
    ],
    out = "release.stripped.tar.zst",
    compression = "zstd",
    tags = PUBLISH_PACKAGING_TAGS,
    visibility = ["//distribution/publish:__pkg__"],
)

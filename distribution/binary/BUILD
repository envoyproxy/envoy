load("@envoy_toolshed//tarball:compressed_tar.bzl", "compressed_tar")
load("@rules_pkg//pkg:mappings.bzl", "pkg_files")
load("//distribution:packages.bzl", "PUBLISH_PACKAGING_TAGS")
load("//distribution/binary:compiler.bzl", "bundled")

licenses(["notice"])  # Apache 2

bundled(
    name = "envoy",
    mode = "opt",
    stripopts = ["--strip-all"],
    targets = {
        "//distribution:envoy-binary-stripped": "envoy",
        "//distribution:envoy-contrib-binary-stripped": "envoy-contrib",
        "//distribution:envoy-binary": "dbg/envoy",
        "//distribution:envoy-dwarf": "dbg/envoy.dwp",
        "//distribution:envoy-contrib-binary": "dbg/envoy-contrib",
        "//distribution:envoy-contrib-dwarf": "dbg/envoy-contrib.dwp",
        "@su-exec": "utils/su-exec",
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

load("@rules_pkg//pkg:mappings.bzl", "pkg_files")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//distribution/binary:compiler.bzl", "bundled")

licenses(["notice"])  # Apache 2

envoy_package()

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
        "//external:su-exec": "utils/su-exec",
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

pkg_tar(
    name = "release",
    srcs = [
        ":release_files",
    ],
    compressor = "//tools/zstd",
    compressor_args = "-T0",
    extension = "tar.zst",
)

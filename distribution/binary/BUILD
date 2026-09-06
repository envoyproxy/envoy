load("@rules_pkg//pkg:mappings.bzl", "pkg_files")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
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

# Test cert/key used by the google-vrp image. The certificate is generated at
# build time (see @envoy_toolshed//certs) so it cannot be ADDed from the Docker
# build context, and is shipped in the release tarball instead.
pkg_files(
    name = "vrp_cert_files",
    srcs = [
        "//test/config/integration/certs:servercert.pem",
        "//test/config/integration/certs:serverkey.pem",
    ],
    prefix = "certs",
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
        ":vrp_cert_files",
    ],
    compressor = "@zstd//:zstd_cli",
    compressor_args = "-T0",
    extension = "tar.zst",
)

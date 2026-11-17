load("@envoy_repo//:version.bzl", "VERSION")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//distribution:packages.bzl", "MAINTAINER", "PUBLISH_PACKAGING_TAGS", "envoy_pkg_distros")
load("//distribution/binary:compiler.bzl", "bundled")

licenses(["notice"])  # Apache 2

envoy_package()

# Everything that a release build publishes for its platform, built in one invocation
# (`ci/do_ci.sh release.publish`):
#
# - the release binaries (`//distribution/binary:release`);
# - the tools;
# - the container images (`//distribution/docker:images`);
# - the distro packages (unsigned, as `//distribution:packages` builds them from the release
#   binaries).

# The stripped binaries of the release (the `envoy` and `envoy-contrib` of
# `//distribution/binary:release`), for the packages. `bundled` sets the same compilation mode and
# strip options as the release, so these are the binaries in the release.
bundled(
    name = "envoy-binary",
    mode = "opt",
    stripopts = ["--strip-all"],
    targets = {"//distribution:envoy-binary-stripped": "envoy"},
)

bundled(
    name = "envoy-contrib-binary",
    mode = "opt",
    stripopts = ["--strip-all"],
    targets = {"//distribution:envoy-contrib-binary-stripped": "envoy-contrib"},
)

# `packages.tar.gz`, as `//distribution:packages.tar.gz` with the release binaries.
envoy_pkg_distros(
    name = "packages",
    # gzip with all the cores (the same format and level).
    compressor = "@envoy_toolshed//pigz",
    compressor_args = "-n",
    envoy_bin = ":envoy-binary",
    envoy_contrib_bin = ":envoy-contrib-binary",
    maintainer = MAINTAINER,
    tags = PUBLISH_PACKAGING_TAGS,
    version = VERSION,
)

filegroup(
    name = "publish",
    srcs = [
        ":packages",
        "//distribution/binary:release",
        "//distribution/docker:images",
        "//tools/config_load_check:config_load_check_tool.stripped",
        "//tools/router_check:router_check_tool.stripped",
        "//tools/schema_validator:schema_validator_tool.stripped",
    ],
)

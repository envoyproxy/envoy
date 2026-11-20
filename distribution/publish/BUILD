load("@envoy_repo//:version.bzl", "VERSION")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//distribution:packages.bzl", "MAINTAINER", "PUBLISH_PACKAGING_TAGS", "envoy_pkg_distros", "release_version_for")
load("//distribution/binary:compiler.bzl", "bundled")
load(":arch.bzl", "arch_file", "arch_files")

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

# The tools, built as the release binaries (and as the container images' tools).
bundled(
    name = "tools",
    mode = "opt",
    stripopts = ["--strip-all"],
    targets = {
        "//tools/config_load_check:config_load_check_tool.stripped": "config_load_check_tool",
        "//tools/router_check:router_check_tool.stripped": "router_check_tool",
        "//tools/schema_validator:schema_validator_tool.stripped": "schema_validator_tool",
    },
)

filegroup(
    name = "publish",
    srcs = [
        ":packages",
        ":tools",
        "//distribution/binary:release",
        "//distribution/docker:images",
    ],
)

# The `.deb`/`.changes` files of the packages (as bundled in `packages.tar.gz`).
filegroup(
    name = "debs_files",
    srcs = [
        ":%s.%s" % (package, kind)
        for package in [
            "envoy",
            "envoy-%s" % release_version_for(VERSION),
            "envoy-contrib",
            "envoy-contrib-%s" % release_version_for(VERSION),
        ]
        for kind in [
            "changes",
            "deb",
        ]
    ],
)

# What `ci/do_ci.sh release.multiarch` builds for each arch: the container images (published), and
# what it signs for both arches, in the same invocation (the `.deb`/`.changes` files, and the
# stripped release binaries).
filegroup(
    name = "publish_multiarch",
    srcs = [
        ":debs_files",
        "//distribution/binary:release_stripped",
        "//distribution/docker:images",
    ],
)

# Everything published for each arch, at `publish_<arch>/<basename>`, from any host: the binaries
# of another arch are cross-compiled (eg arm64 from an x64 host).
[
    arch_files(
        name = "%s_%s" % (name, arch),
        srcs = [":%s" % name],
        platform = "//bazel/platforms:linux_%s" % arch,
    )
    for name in [
        "publish",
        "publish_multiarch",
    ]
    for arch in [
        "arm64",
        "x64",
    ]
]

# The signing inputs of each arch, for `ci/do_ci.sh release.multiarch`, which signs in the same
# invocation (`--//distribution:<arch>-{debs,release}`).
[
    arch_file(
        name = "publish_multiarch_%s_release" % arch,
        src = ":publish_multiarch_%s" % arch,
        basename = "release.stripped.tar.zst",
    )
    for arch in [
        "arm64",
        "x64",
    ]
]

[
    arch_files(
        name = "publish_multiarch_%s_debs" % arch,
        srcs = [":debs_files"],
        platform = "//bazel/platforms:linux_%s" % arch,
    )
    for arch in [
        "arm64",
        "x64",
    ]
]

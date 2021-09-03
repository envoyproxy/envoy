load("@rules_pkg//:pkg.bzl", "pkg_deb", "pkg_tar")

GLIBC_MIN_VERSION = "2.27"

def envoy_pkg_deb(
        name,
        data,
        homepage = "https://www.envoyproxy.io/",
        description = "Envoy built for Debian/Ubuntu",
        preinst = "//distribution/debian:preinst",
        supported_distributions = "buster bullseye bionic focal hirstute impish",
        architecture = select({
            "//bazel:x86": "amd64",
            "//conditions:default": "arm64",
        }),
        depends = [
            "libc6 (>= %s)" % GLIBC_MIN_VERSION,
        ],
        version = None,
        maintainer = None,
        **kwargs):
    """Wrapper for `pkg_deb` with Envoy defaults"""
    pkg_deb(
        name = "%s.deb" % name,
        architecture = architecture,
        data = data,
        depends = depends,
        description = description,
        distribution = supported_distributions,
        homepage = homepage,
        maintainer = maintainer,
        package = name,
        version = version,
        preinst = preinst,
        **kwargs
    )

def envoy_pkg_debs(name, version, release_version, maintainer, bin_files = ":envoy-bin-files", config = ":envoy-config"):
    """Package the Envoy .debs with their .changes files.

    Packages are created for the version *and* the release version, eg

    - envoy_1.21.0_amd64.deb
    - envoy-1.21_1.21.0_amd64.deb

    This way packages are available for both "envoy" and "envoy-1.21" in package managers.
    """

    # generate deb data for all packages
    pkg_tar(
        name = "deb-data",
        srcs = [
            "//distribution/debian:copyright",
            config,
            bin_files,
        ],
        remap_paths = {"/copyright": "/usr/share/doc/envoy/copyright"},
    )

    # generate package for this patch version
    envoy_pkg_deb(
        name = "envoy",
        data = ":deb-data",
        version = version,
        maintainer = maintainer,
    )

    # generate package for this minor version
    envoy_pkg_deb(
        name = "envoy-%s" % release_version,
        data = ":deb-data",
        version = version,
        conflicts = ["envoy"],
        provides = ["envoy"],
        maintainer = maintainer,
    )

    # TODO(phlax): Remove this hack
    #   Due to upstream issues with `OutputGroupInfo` files from `pkg_deb`
    #   we have to follow the real filepath of the .deb file to find the
    #   .changes file (~artefact)
    #   For this hack to work, strategy needs to be `sandbox,local` for the
    #   following mnemonics:
    #   - Genrule
    #   - MakeDeb
    #
    #   Upstream issue is here: https://github.com/bazelbuild/rules_pkg/issues/477
    #

    deb_files = (
        "envoy_deb1=$$(realpath $(location :envoy.deb)) " +
        "&& envoy_deb2=$$(realpath $(location :envoy-%s.deb)) \\" % release_version
    )

    # bundle all debs and changes files into /debs folder of tarball
    native.genrule(
        name = name,
        srcs = ["envoy.deb", "envoy-%s.deb" % release_version],
        outs = [":debs.tar"],
        cmd = deb_files + """
        && tar --transform "flags=r;s|^|deb/|" \
             -C $$(dirname $$envoy_deb1) \
             -cf $@ \
             $$(basename $${envoy_deb1}) \
             $$(basename $${envoy_deb1%.deb}.changes) \
             $$(basename $${envoy_deb2}) \
             $$(basename $${envoy_deb2%.deb}.changes)
        """,
    )

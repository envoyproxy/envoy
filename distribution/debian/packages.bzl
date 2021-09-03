load("@rules_pkg//:pkg.bzl", "pkg_deb", "pkg_tar")

GLIBC_MIN_VERSION = "2.27"

def envoy_pkg_deb(name = "envoy", version = None, data = ":deb-data.tar.xz", maintainer = None, **kwargs):
    arch = select({
        "//bazel:x86": "amd64",
        "//conditions:default": "arm64",
    })
    pkg_deb(
        name = "%s.deb" % name,
        architecture = arch,
        data = data,
        depends = [
            "libc6 (>= %s)" % GLIBC_MIN_VERSION,
        ],
        description = "Envoy built for Debian/Ubuntu",
        distribution = "buster bullseye impish hirstute",
        homepage = "https://www.envoyproxy.io/",
        maintainer = maintainer,
        package = name,
        version = version,
        # d = "%s_%s_%s.changes" % (name, version, arch),
        changes = "%s_%s.changes" % (name, version),
        preinst = "//distribution/debian:preinst",
        **kwargs,
    )

def envoy_deb_data(bin_src = None, suffix = ""):
    remap_paths = {}
    if suffix:
        remap_paths["/envoy.%s" % suffix] =  "/envoy"
        suffix = "-%s" % suffix

    pkg_tar(
        name = "bin%s" % suffix,
        extension = "tar",
        package_dir = "/usr/bin",
        srcs = [bin_src],
        mode = "0755",
        remap_paths = remap_paths,
    )

    pkg_tar(
        name = "deb-data%s" % suffix,
        extension = "tar.xz",
        deps = [
            ":config.tar",
            ":bin%s.tar" % suffix,
            ":copyright.tar",
        ],
    )

def envoy_pkg_debs(version = None, release_version = None, envoy_bin = None, maintainer = None):
    pkg_tar(
        name = "copyright",
        extension = "tar",
        srcs = ["//distribution/debian:copyright"],
        package_dir = "/usr/share/doc/envoy",
    )

    # generate deb data for base and dbg builds
    envoy_deb_data(bin_src = envoy_bin)

    # generate packages for this patch version
    envoy_pkg_deb(version = version, maintainer = maintainer)

    # generate packages for this minor version
    envoy_pkg_deb(
        name = "envoy-%s" % release_version,
        version = version,
        conflicts = ["envoy"],
        provides = ["envoy"],
        maintainer = maintainer)

    debs = (
        "envoy.deb",
        "envoy-%s.deb" % release_version)

    changes = (
        "envoy_%s.changes" % version,
        "envoy-%s_%s.changes" % (release_version, version))

    # package all debs and changes files
    pkg_tar(
        name = "base_debs",
        extension = "tar",
        package_dir = "deb",
        srcs = [":%s" % deb for deb in debs + changes],
    )

    # select(arch) cant be used in the changes filepath so mangle it here
    arch = select({
        "//bazel:x86": "amd64",
        "//conditions:default": "arm64",
    })

    deb_mangle_cmd = "rm -rf /tmp/debs && mkdir -p /tmp/debs && tar xf $< -C /tmp/debs "
    for change in changes:
        change_root = ".".join(change.split(".")[:-1])
        deb_mangle_cmd += "&& mv /tmp/debs/deb/%s /tmp/debs/deb/%s_" % (change, change_root)
        deb_mangle_cmd += arch + ".changes"
    deb_mangle_cmd += "&& tar cf $@ -C /tmp/debs ."

    native.genrule(
        name = "debs",
        srcs = [":base_debs"],
        outs = [":debs.tar"],
        cmd = deb_mangle_cmd,
    )

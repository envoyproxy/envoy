load("@rules_pkg//:pkg.bzl", "pkg_deb", "pkg_tar")
load("@com_github_google_rpmpack//:def.bzl", "pkg_tar2rpm")

def pkg_distro(
        name = None,
        dockerfile = "debian",
        tag = "",
        distro = "",
        visibility = ["//visibility:public"]):

    build_srcs = [
        ":Dockerfile.%s" % dockerfile,
    ]

    if tag or distro:
        env_command = []

        if tag:
            env_command += ["echo 'tag: \"" + tag + "\"' >> $@"]

        if distro:
            env_command += ["echo 'distro: \'" + distro + "\'' >> $@"]

        native.genrule(
            name = "build-env-" + name,
            cmd = "\n".join(env_command),
            outs = [name + "/env"],
        )
        build_srcs += [":build-env-" + name]

    pkg_tar(
        name = "build-" + name,
        extension = "tar",
        package_dir = name,
        srcs = build_srcs,
        remap_paths = {
            "/Dockerfile." + dockerfile: "/Dockerfile",
        },
    )

def envoy_rpm_data(envoy_bin):
    native.genrule(
        name = "envoy-libcxx-bin",
        srcs = [envoy_bin],
        outs = [":envoy-libcxx"],
        cmd = "cp -Lv $(location %s) $@" % envoy_bin + select({
            "//builds/lib:bundle_libcpp": " && patchelf --set-rpath '$$ORIGIN/../lib' $@",
            "//conditions:default": "",
        }),
    )

    pkg_tar(
        name = "tar-libcxx-data",
        srcs = [":envoy-libcxx-bin"],
        deps = select({
            "//builds/lib:bundle_libcpp": ["//builds/lib:libcxx"],
            "//conditions:default": [],
        }),
        remap_paths = {
            "/envoy-libcxx": "/usr/bin/envoy",
        },
    )

    pkg_tar(
        name = "tar-rpm-data",
        deps = [
            ":tar-libcxx-data",
            ":config.tar",
        ],
    )

def envoy_pkg_tar2rpm(name = "envoy", version = None):
    pkg_tar2rpm(
        name = "%s_%s" %(name, version),
        data = ":tar-rpm-data",
        pkg_name = name,
        release = "pre",
        version = version,
        prein = """
        getent group envoy >/dev/null 2>&1 || {
            groupadd --system envoy >/dev/null
        }
        getent passwd envoy >/dev/null 2>&1 || {
            adduser \
                -c "envoy user" \
                -g envoy \
                -d /nonexistent \
                -s /bin/false \
                --system \
                --no-create-home \
              envoy > /dev/null
        }
        """,
    )

def envoy_pkg_rpms(version = None, release_version = None, envoy_bin = None):
    envoy_rpm_data(envoy_bin)
    envoy_pkg_tar2rpm(version = version)
    envoy_pkg_tar2rpm(name = "envoy-%s" % release_version, version = version)

    rpms = (
        "envoy_%s.rpm" % version,
        "envoy-%s_%s.rpm" % (release_version, version))

    pkg_tar(
        name = "base_rpms",
        extension = "tar",
        package_dir = "rpm",
        srcs = [":%s" % rpm for rpm in rpms],
    )

    # tar2rpm doesnt use the arch in deriving the package filename and select
    # cant be used in a rule name or output, so mangle the filepaths here
    arch = select({
        "//bazel:x86": "x64",
        "//conditions:default": "arm64",
    })

    rpm_mangle_cmd = "rm -rf /tmp/rpms && mkdir -p /tmp/rpms && tar xf $< -C /tmp/rpms "
    for rpm in rpms:
        rpm_root = ".".join(rpm.split(".")[:-1])
        rpm_mangle_cmd += "&& mv /tmp/rpms/rpm/%s /tmp/rpms/rpm/%s_" % (rpm, rpm_root)
        rpm_mangle_cmd += arch + ".rpm"
    rpm_mangle_cmd += "&& tar cf $@ -C /tmp/rpms ."

    native.genrule(
        name = "rpms",
        srcs = [":base_rpms"],
        outs = [":rpms.tar"],
        cmd = rpm_mangle_cmd,
    )

def envoy_pkg_deb(name = "envoy", version = None, data = ":deb-data.tar.xz", **kwargs):
    arch = select({
        "//bazel:x86": "amd64",
        "//conditions:default": "arm64",
    })
    pkg_deb(
        name = "%s.deb" % name,
        architecture = arch,
        data = data,
        description = "Envoy built for Debian/Ubuntu",
        distribution = "buster bullseye impish hirstute",
        homepage = "https://www.envoyproxy.io/",
        maintainer = "Envoy maintainers <envoy-maintainers@googlegroups.com>",
        package = name,
        version = version,
        # d = "%s_%s_%s.changes" % (name, version, arch),
        changes = "%s_%s.changes" % (name, version),
        preinst = "debian/preinst",
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

def envoy_pkg_debs(version = None, release_version = None, envoy_bin = None, envoy_debug_bin = None):
    pkg_tar(
        name = "copyright",
        extension = "tar",
        srcs = [":deb-copyright"],
        remap_paths = {
            "/deb-copyright": "/copyright",
        },
        package_dir = "/usr/share/doc/envoy",
    )

    # generate deb data for base and dbg builds
    envoy_deb_data(bin_src = envoy_bin)
    envoy_deb_data(bin_src = envoy_debug_bin, suffix = "dbg")

    # generate packages for this patch version
    envoy_pkg_deb(version = version)
    envoy_pkg_deb(
        name = "envoy-dbg",
        version = version,
        conflicts = ["envoy"],
        provides = ["envoy"],
        data = ":deb-data-dbg.tar.xz")

    # generate packages for this minor version
    envoy_pkg_deb(
        name = "envoy-%s" % release_version,
        version = version,
        conflicts = ["envoy"],
        provides = ["envoy"])
    envoy_pkg_deb(
        name = "envoy-%s-dbg" % release_version,
        version = version,
        conflicts = ["envoy"],
        provides = ["envoy"],
        data = ":deb-data-dbg.tar.xz")

    debs = (
        "envoy.deb",
        "envoy-dbg.deb",
        "envoy-%s.deb" % release_version,
        "envoy-%s-dbg.deb" % release_version)

    changes = (
        "envoy_%s.changes" % version,
        "envoy-dbg_%s.changes" % version,
        "envoy-%s_%s.changes" % (release_version, version),
        "envoy-%s-dbg_%s.changes" % (release_version, version))

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

def envoy_common_data():
    pkg_tar(
        name = "config",
        extension = "tar",
        srcs = ["//configs:envoyproxy_io_proxy.yaml"],
        remap_paths = {
            "/envoyproxy_io_proxy.yaml": "/envoy.yaml",
        },
        package_dir = "/etc/envoy",
    )

def envoy_pkg_distros(version = None, envoy_bin = ":envoy-bin", envoy_debug_bin = ":envoy-dbg-bin"):
    if "-" in version:
        version, version_suffix = version.split("-")

    major, minor, patch = version.split(".")
    release_version = ".".join((major, minor))

    envoy_common_data()
    envoy_pkg_debs(
        version = version,
        release_version = release_version,
        envoy_bin = envoy_bin,
        envoy_debug_bin = envoy_debug_bin)
    envoy_pkg_rpms(
        version = version,
        release_version = release_version,
        envoy_bin = envoy_bin)

    pkg_tar(
        name = "build",
        extension = "tar.gz",
        deps = [
            ":debs.tar",
            ":rpms.tar",
        ],
    )

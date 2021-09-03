load("@com_github_google_rpmpack//:def.bzl", "pkg_tar2rpm")
load("@rules_pkg//:pkg.bzl", "pkg_tar")

def envoy_rpm_data(envoy_bin):
    native.genrule(
        name = "envoy-libcxx-bin",
        srcs = [envoy_bin],
        outs = [":envoy-libcxx"],
        cmd = "cp -Lv $(location %s) $@" % envoy_bin + select({
            "//distribution/rpm/lib:bundle_libcpp": " && patchelf --set-rpath '$$ORIGIN/../lib' $@",
            "//conditions:default": "",
        }),
    )

    pkg_tar(
        name = "tar-libcxx-data",
        srcs = [":envoy-libcxx-bin"],
        deps = select({
            "//distribution/rpm/lib:bundle_libcpp": ["//distribution/rpm/lib:libcxx"],
            "//conditions:default": [],
        }),
        remap_paths = {
            "/envoy-libcxx": "/usr/bin/envoy",
        },
    )

    pkg_tar(
        name = "base-tar-rpm-data",
        deps = [
            ":tar-libcxx-data",
            ":config.tar",
        ],
    )

    native.genrule(
        name = "tar-rpm-data",
        tools = ["//tools/distribution:strip_dirs"],
        srcs = [":base-tar-rpm-data"],
        cmd = """
        $(location //tools/distribution:strip_dirs) \
            $(location :base-tar-rpm-data) \
            $@
        """,
        outs = ["tar-rpm-data.tar"],
    )

def envoy_pkg_tar2rpm(name = "envoy", version = None, maintainer = None):
    arch = select({
        "//bazel:x86": "x86_64",
        "//conditions:default": "aarch64",
    })
    pkg_tar2rpm(
        name = "%s_%s" %(name, version),
        data = ":tar-rpm-data",
        packager = maintainer,
        arch = arch,
        pkg_name = name,
        # release = "pre",
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

def envoy_pkg_rpms(version = None, release_version = None, envoy_bin = None, maintainer = None):
    envoy_rpm_data(envoy_bin)
    envoy_pkg_tar2rpm(version = version, maintainer = maintainer)
    envoy_pkg_tar2rpm(name = "envoy-%s" % release_version, version = version, maintainer = maintainer)

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
        "//bazel:x86": "x86_64",
        "//conditions:default": "aarch64",
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

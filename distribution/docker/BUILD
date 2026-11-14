load("@bazel_skylib//rules:write_file.bzl", "write_file")
load("@envoy_toolshed//oci:defs.bzl", "oci_base_edit")
load("@rules_distroless//distroless:defs.bzl", "cacerts", "flatten")
load("@rules_oci//oci:defs.bzl", "oci_image")
load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes", "pkg_files", "pkg_mkdirs", "pkg_mklink")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("//distribution/binary:compiler.bzl", "bundled")

licenses(["notice"])  # Apache 2

exports_files([
    "Dockerfile-envoy",
    "docker-entrypoint.sh",
    "build.sh",
])

# Container images.
#
# These match the stages of `Dockerfile-envoy`. Each image is built for the target platform with
# the release binaries, as an OCI tarball (`<image>.tar`, eg `envoy-distroless.tar`).
#
# The `envoy`, `envoy-debug`, `envoy-contrib`, `envoy-contrib-debug` and `envoy-tools` images are
# based on ubuntu (`@ubuntu_base`), with packages from an Ubuntu snapshot (`@ubuntu_jammy`). The
# `envoy-distroless` and `envoy-contrib-distroless` images are based on distroless
# (`@distroless_base_nossl`).

ROOT = pkg_attributes(
    group = "root",
    mode = "0644",
    user = "root",
)

ROOT_EXECUTABLE = pkg_attributes(
    group = "root",
    mode = "0755",
    user = "root",
)

# Binaries, built as for the release (`//distribution/binary:envoy`).
BINARIES = {
    "envoy": "//distribution:envoy-binary-stripped",
    "envoy-contrib": "//distribution:envoy-contrib-binary-stripped",
    "envoy-contrib-debug": "//distribution:envoy-contrib-binary",
    "envoy-contrib-dwarf": "//distribution:envoy-contrib-dwarf",
    "envoy-debug": "//distribution:envoy-binary",
    "envoy-dwarf": "//distribution:envoy-dwarf",
    "su-exec": "@su-exec",
    "config_load_check_tool": "//test/tools/config_load_check:config_load_check_tool.stripped",
    "router_check_tool": "//test/tools/router_check:router_check_tool.stripped",
    "schema_validator_tool": "//test/tools/schema_validator:schema_validator_tool.stripped",
}

[
    bundled(
        name = "%s-bin" % name,
        mode = "opt",
        stripopts = ["--strip-all"],
        targets = {binary: name},
    )
    for name, binary in BINARIES.items()
]

# Layers

pkg_mkdirs(
    name = "etc_envoy",
    attributes = pkg_attributes(
        group = "root",
        mode = "0755",
        user = "root",
    ),
    dirs = ["etc/envoy"],
)

pkg_files(
    name = "envoy_config",
    srcs = ["//configs:envoyproxy_io_proxy.yaml"],
    attributes = ROOT,
    prefix = "etc/envoy",
    renames = {"//configs:envoyproxy_io_proxy.yaml": "envoy.yaml"},
)

pkg_tar(
    name = "envoy_config_layer",
    srcs = [
        ":envoy_config",
        ":etc_envoy",
    ],
)

pkg_files(
    name = "envoy_version",
    srcs = ["//:VERSION.txt"],
    attributes = ROOT,
    prefix = "etc/envoy",
)

pkg_tar(
    name = "envoy_version_layer",
    srcs = [
        ":envoy_version",
        ":etc_envoy",
    ],
)

pkg_files(
    name = "entrypoint",
    srcs = ["docker-entrypoint.sh"],
    attributes = ROOT_EXECUTABLE,
)

pkg_files(
    name = "su_exec",
    srcs = [":su-exec-bin"],
    attributes = ROOT_EXECUTABLE,
    prefix = "usr/local/bin",
    renames = {":su-exec-bin": "su-exec"},
)

pkg_tar(
    name = "entrypoint_layer",
    srcs = [
        ":entrypoint",
        ":su_exec",
    ],
)

# `/usr/local/bin/envoy`, and its debug info, for each image.
[
    pkg_files(
        name = "%s_binary_files" % name,
        srcs = [":%s-bin" % binary],
        attributes = ROOT_EXECUTABLE,
        prefix = "usr/local/bin",
        renames = {":%s-bin" % binary: "envoy"},
    )
    for name, binary in [
        ("envoy", "envoy"),
        ("envoy-contrib", "envoy-contrib"),
        ("envoy-debug", "envoy-debug"),
        ("envoy-contrib-debug", "envoy-contrib-debug"),
    ]
]

[
    pkg_files(
        name = "%s_dwarf_files" % name,
        srcs = [":%s-dwarf-bin" % name],
        attributes = ROOT_EXECUTABLE,
        prefix = "usr/local/bin",
        renames = {":%s-dwarf-bin" % name: "%s.dwp" % name},
    )
    for name in [
        "envoy",
        "envoy-contrib",
    ]
]

[
    pkg_tar(
        name = "%s_binary_layer" % name,
        srcs = [":%s_binary_files" % name] + extra,
    )
    for name, extra in [
        ("envoy", []),
        ("envoy-contrib", []),
        (
            "envoy-debug",
            [":envoy_dwarf_files"],
        ),
        (
            "envoy-contrib-debug",
            [":envoy-contrib_dwarf_files"],
        ),
    ]
]

[
    pkg_files(
        name = "%s_files" % tool,
        srcs = [":%s-bin" % tool],
        attributes = ROOT_EXECUTABLE,
        prefix = "usr/local/bin",
        renames = {":%s-bin" % tool: tool},
    )
    for tool in [
        "config_load_check_tool",
        "router_check_tool",
        "schema_validator_tool",
    ]
]

pkg_tar(
    name = "tools_layer",
    srcs = [
        ":config_load_check_tool_files",
        ":router_check_tool_files",
        ":schema_validator_tool_files",
    ],
)

# Ubuntu base layers.
#
# These replace the `RUN` steps of the `envoy-base` stage (`adduser --group --system envoy`, and
# `apt-get install ca-certificates tzdata`), which can not run in bazel.

# The `envoy` system user and group, as `adduser --group --system envoy` adds them, and the
# packages, in the dpkg status of the base.
oci_base_edit(
    name = "base_edit",
    image = "@ubuntu_base",
    packages = [
        "@ubuntu_jammy//ca-certificates:control",
        "@ubuntu_jammy//openssl:control",
        "@ubuntu_jammy//tzdata:control",
    ],
    users = [{
        "group": True,
        "name": "envoy",
    }],
)

# The packages, without their maintainer scripts, which would create the certificate bundle and
# the timezone settings.
flatten(
    name = "packages_layer",
    deduplicate = True,
    tars = [
        "@ubuntu_jammy//ca-certificates:data",
        "@ubuntu_jammy//openssl:data",
        "@ubuntu_jammy//tzdata:data",
    ],
)

cacerts(
    name = "cacerts",
    package = "@ubuntu_jammy//ca-certificates:data",
)

write_file(
    name = "timezone_content",
    out = "etc_timezone",
    content = [
        "Etc/UTC",
        "",
    ],
)

pkg_files(
    name = "timezone",
    srcs = [":timezone_content"],
    attributes = ROOT,
    prefix = "etc",
    renames = {":timezone_content": "timezone"},
)

pkg_mklink(
    name = "localtime",
    link_name = "etc/localtime",
    target = "/usr/share/zoneinfo/Etc/UTC",
)

pkg_tar(
    name = "timezone_layer",
    srcs = [
        ":localtime",
        ":timezone",
    ],
)

UBUNTU_LAYERS = [
    ":base_edit",
    ":envoy_version_layer",
    ":packages_layer",
    ":cacerts",
    ":timezone_layer",
    ":envoy_config_layer",
    ":entrypoint_layer",
]

# Images

[
    oci_image(
        name = name,
        base = "@ubuntu_base",
        cmd = [
            "envoy",
            "-c",
            "/etc/envoy/envoy.yaml",
        ],
        entrypoint = ["/docker-entrypoint.sh"],
        env = {"DEBIAN_FRONTEND": "noninteractive"},
        exposed_ports = ["10000/tcp"],
        tars = UBUNTU_LAYERS + [":%s_binary_layer" % binary] + extra,
    )
    for name, binary, extra in [
        ("envoy", "envoy", []),
        ("envoy-debug", "envoy-debug", []),
        ("envoy-contrib", "envoy-contrib", []),
        ("envoy-contrib-debug", "envoy-contrib-debug", []),
        (
            "envoy-tools",
            "envoy",
            [":tools_layer"],
        ),
    ]
]

[
    oci_image(
        name = "%s-distroless" % name,
        base = "@distroless_base_nossl",
        cmd = [
            "-c",
            "/etc/envoy/envoy.yaml",
        ],
        entrypoint = ["/usr/local/bin/envoy"],
        exposed_ports = ["10000/tcp"],
        tars = [
            ":envoy_config_layer",
            ":%s_binary_layer" % name,
        ],
    )
    for name in [
        "envoy",
        "envoy-contrib",
    ]
]

IMAGES = [
    "envoy",
    "envoy-contrib",
    "envoy-contrib-debug",
    "envoy-contrib-distroless",
    "envoy-debug",
    "envoy-distroless",
    "envoy-tools",
]

# OCI tarballs of the images: the image layout, with `index.json` referring to the image manifest
# (as `docker buildx build -o type=oci` for a single platform).
[
    genrule(
        name = "%s_tar" % image,
        srcs = [":%s" % image],
        outs = ["%s.tar" % image],
        cmd = """
        tar --dereference --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \\
            -cf $@ -C $(location :%s) .
        """ % image,
    )
    for image in IMAGES
]

filegroup(
    name = "images",
    srcs = [":%s_tar" % image for image in IMAGES],
)

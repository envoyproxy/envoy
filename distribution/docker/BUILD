load("@rules_oci//oci:defs.bzl", "oci_image")
load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes", "pkg_files", "pkg_mkdirs")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("//distribution/binary:compiler.bzl", "bundled")

licenses(["notice"])  # Apache 2

exports_files([
    "Dockerfile-envoy",
    "docker-entrypoint.sh",
    "build.sh",
])

# Distroless container images.
#
# These match the `envoy-distroless` stage of `Dockerfile-envoy`. The OCI tarballs
# (`envoy-distroless.tar`, `envoy-contrib-distroless.tar`) are built for the target platform with
# the release binaries, and collected with the other images built with `docker buildx`.

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
    attributes = pkg_attributes(
        group = "root",
        mode = "0644",
        user = "root",
    ),
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

# The binaries are built as for the release (`//distribution/binary:envoy`).
[
    bundled(
        name = "%s-binary" % name,
        mode = "opt",
        stripopts = ["--strip-all"],
        targets = {binary: "envoy"},
    )
    for name, binary in [
        ("envoy", "//distribution:envoy-binary-stripped"),
        ("envoy-contrib", "//distribution:envoy-contrib-binary-stripped"),
    ]
]

[
    pkg_files(
        name = "%s_binary_files" % name,
        srcs = [":%s-binary" % name],
        attributes = pkg_attributes(
            group = "root",
            mode = "0755",
            user = "root",
        ),
        prefix = "usr/local/bin",
        renames = {":%s-binary" % name: "envoy"},
    )
    for name in [
        "envoy",
        "envoy-contrib",
    ]
]

[
    pkg_tar(
        name = "%s_binary_layer" % name,
        srcs = [":%s_binary_files" % name],
    )
    for name in [
        "envoy",
        "envoy-contrib",
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

# OCI tarballs of the images: the image layout, with `index.json` referring to the image manifest
# (as `docker buildx build -o type=oci` for a single platform).
[
    genrule(
        name = "%s-distroless_tar" % name,
        srcs = [":%s-distroless" % name],
        outs = ["%s-distroless.tar" % name],
        cmd = """
        tar --dereference --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \\
            -cf $@ -C $(location :%s-distroless) .
        """ % name,
    )
    for name in [
        "envoy",
        "envoy-contrib",
    ]
]

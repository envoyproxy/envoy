load("@rules_pkg//pkg:mappings.bzl", "pkg_filegroup", "pkg_files", "strip_prefix")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("//tools/base:envoy_python.bzl", "envoy_entry_point")

def envoy_example(
        name,
        shared = True,
        visibility = ["//visibility:public"]):

    if shared:
        shared_deps = [":shared"]
    else:
        shared_deps = []

    native.sh_binary(
        name = "verify",
        srcs = ["verify.sh"],
    )

    native.filegroup(
        name = "files",
        srcs = native.glob(["**/*"]),
    )

    pkg_tar(
        name = "example_pkg",
        srcs = [":srcs"],
        package_dir = name,
    )

    pkg_files(
        name = "srcs",
        srcs = [":files"],
        strip_prefix = strip_prefix.from_pkg(),
    )

    pkg_files(
        name = "shared",
        srcs = ["//examples:shared"],
        strip_prefix = strip_prefix.from_pkg(),
    )

    pkg_tar(
        name = "example",
        srcs = [
            "//examples:verify-common",
            ":verify",
        ] + shared_deps,
        deps = [":example_pkg"],
    )

    envoy_entry_point(
        name = "docker-compose",
        pkg = "docker-compose",
    )

    native.genrule(
        name = "verification",
        cmd = """
        tmp=$$(mktemp -d --tmpdir=/tmp/bazel-shared) \
        && export DOCKER_COMPOSE=$$(realpath $(location :docker-compose)) \
        && out=$$(realpath $@) \
        && chmod +rx $$tmp \
        && tar xf $(location :example) -C $$tmp \
        && cd $$tmp/%s \
        && chmod +x verify.sh \
        && ./verify.sh \
        && echo done > $$out 2>&1
        """ % name,
        tools = [
            ":example",
            ":docker-compose",
        ],
        outs = ["%s-verification.txt" % name],
        visibility = visibility,
    )

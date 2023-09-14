load("@base_pip3//:requirements.bzl", "requirement")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("@envoy//tools/base:envoy_python.bzl", "envoy_entry_point")
load("@rules_pkg//pkg:mappings.bzl", "pkg_filegroup", "pkg_files")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_entry_point(
    name = "sphinx",
    pkg = "sphinx",
    script = "sphinx-build",
    deps = [
        requirement("sphinx_rtd_theme"),
        requirement("sphinxcontrib.googleanalytics"),
        requirement("sphinxcontrib.httpdomain"),
    ],
)

pkg_files(
    name = "sphinx_base",
    srcs = [":conf.py"],
    strip_prefix = "/docs",
)

pkg_files(
    name = "sphinx_root",
    srcs = glob([
        "root/**/*.css",
        "root/**/*.ico",
        "root/**/*.js",
        "root/**/*.json",
        "root/**/*.pb",
        "root/**/*.png",
        "root/**/*.jpg",
        "root/**/*.proto",
        "root/**/*.rst",
        "root/**/*.svg",
        "root/**/*.yaml",
    ]),
    strip_prefix = "/docs/root",
)

pkg_filegroup(
    name = "rst_files",
    srcs = [
        ":sphinx_base",
        ":sphinx_root",
    ],
)

pkg_tar(
    name = "rst",
    srcs = [":rst_files"],
    extension = "tar.gz",
)

genrule(
    name = "docs",
    outs = ["docs.tar.gz"],
    cmd = """
    mkdir -p build \
    && tar xf $(location :rst) -C build \
    && $(location :sphinx) \
         -W \
         --keep-going \
         -b html \
         build \
         output \
    && tar czf $@ -C output .
    """,
    tools = [
        ":rst",
        ":sphinx",
    ],
)

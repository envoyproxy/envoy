load("@base_pip3//:requirements.bzl", "requirement")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy//tools/base:envoy_python.bzl", "envoy_entry_point")
load("@envoy//tools/python:namespace.bzl", "envoy_py_namespace")
load("@rules_pkg//pkg:mappings.bzl", "pkg_filegroup", "pkg_files")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_py_namespace()

envoy_entry_point(
    name = "sphinx",
    init_data = [
        "//:py-init",
        ":py-init",
    ],
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
    name = "html_release",
    outs = ["html_release.tar.gz"],
    cmd = """
    . $(location @envoy//bazel:volatile_env) \
    && VERSION_NUMBER="$$(cat $(location //:VERSION))" \
    && export ENVOY_BLOB_SHA=$${BUILD_SHA:-$${ENVOY_BUILD_SCM_REVISION:-$${{BUILD_SCM_REVISION}}} \
    && export ENVOY_DOCS_VERSION_STRING="$${VERSION_NUMBER}"-"$${ENVOY_BLOB_SHA:0:6}" \
    && export ENVOY_DOCS_RELEASE_LEVEL=pre-release \
    && mkdir -p build \
    && tar xf $(location :rst) -C build \
    && $(location :sphinx) \
         -W \
         --keep-going \
         -b html \
         build \
         output \
    && tar czf $@ -C output .
    """,
    stamp = 1,
    tools = [
        ":rst",
        ":sphinx",
        "//:VERSION",
        "@envoy//bazel:volatile_env",
    ],
)

genrule(
    name = "html",
    outs = ["html.tar.gz"],
    cmd = """
    mkdir -p build \
    && VERSION_NUMBER="$$(cat $(location //:VERSION))" \
    && export ENVOY_BLOB_SHA="$${BUILD_SHA:-UNKNOWN}" \
    && export ENVOY_DOCS_VERSION_STRING="$${VERSION_NUMBER}"-"$${ENVOY_BLOB_SHA:0:6}" \
    && export ENVOY_DOCS_RELEASE_LEVEL=pre-release \
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
        "//:VERSION",
    ],
)

alias(
    name = "docs",
    actual = ":html_release",
)

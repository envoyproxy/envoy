load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package([
    "//source/server/admin:__subpackages__",
    "//test/integration/admin_html:__subpackages__",
])

genrule(
    name = "generate_admin_html",
    srcs = [
        "admin_head_start.html",
        "admin.css",
        "active_params.html",
        "active_stats.js",
        "histograms.js",
    ],
    outs = ["admin_html_gen.h"],
    cmd = "./$(location :generate_admin_html.sh) \
        $(location admin_head_start.html) $(location admin.css) $(location active_stats.js) \
        $(location active_params.html) $(location histograms.js) > $@",
    tools = [":generate_admin_html.sh"],
)

# This filegroup is declared here to make it easier to test -debug mode for
# test/integration/admin_html/test_server.
filegroup(
    name = "admin_web_files",
    srcs = [
        "active_params.html",
        "active_stats.js",
        "admin.css",
        "admin_head_start.html",
        "histograms.js",
    ],
)

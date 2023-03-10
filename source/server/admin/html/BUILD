load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package(["//source/server/admin:__subpackages__"])

genrule(
    name = "generate_admin_html",
    srcs = [
        "admin_head_start.html",
        "admin.css",
        "active_params.html",
        "active_stats.js",
    ],
    outs = ["admin_html_gen.h"],
    cmd = "./$(location :generate_admin_html.sh) \
        $(location admin_head_start.html) $(location admin.css) $(location active_stats.js) \
        $(location active_params.html) > $@",
    tools = [":generate_admin_html.sh"],
)

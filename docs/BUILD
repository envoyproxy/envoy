load("@bazel_skylib//rules:common_settings.bzl", "bool_flag", "string_flag")
load("@envoy_toolshed//dependency:macros.bzl", "module_deps_json", "module_updater")
load("@rules_pkg//pkg:mappings.bzl", "pkg_filegroup", "pkg_files")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("//bazel:sphinx_html.bzl", "sphinx_html")

licenses(["notice"])  # Apache 2

# Emit `foo` rather than `foo.html` links. Off by default so PR previews,
# local builds and anything rendered straight from a file/object store keep
# working. envoy-website turns it on and serves `/foo` from `foo.html`.
bool_flag(
    name = "pretty_links",
    build_setting_default = False,
    visibility = ["//visibility:public"],
)

string_flag(
    name = "sphinx_args",
    build_setting_default = "",
    visibility = ["//visibility:public"],
)

string_flag(
    name = "build_sha",
    build_setting_default = "",
    visibility = ["//visibility:public"],
)

string_flag(
    name = "docs_tag",
    build_setting_default = "",
    visibility = ["//visibility:public"],
)

config_setting(
    name = "with_pretty_links",
    flag_values = {":pretty_links": "True"},
)

_LINK_SUFFIX_ENV = select({
    ":with_pretty_links": "export ENVOY_DOCS_LINK_SUFFIX=''; ",
    "//conditions:default": "",
})

exports_files([
    ".bazelrc",
    "MODULE.bazel",
    "MODULE.bazel.lock",
    "v2_mapping.json",
    "empty_extensions.json",
    "redirects.txt",
])

module_deps_json(
    name = "module_deps",
    lockfile = "//:MODULE.bazel.lock",
    module_file = "//:MODULE.bazel",
)

module_updater(
    name = "update_module",
    bazelrc = "@envoy//:.bazelrc",
    dependencies = ":module_deps",
    module_file = "//:MODULE.bazel",
)

filegroup(
    name = "configs",
    srcs = glob(
        ["root/**/*.yaml"],
        exclude = [
            "root/_configs/go/*",
            "root/**/envoy-dynamic-cds-demo.yaml",
            "root/**/envoy-dynamic-lds-demo.yaml",
            "root/**/envoy-dynamic-filesystem-demo.yaml",
            "root/operations/_include/traffic_tapping_*.yaml",
            "root/configuration/http/http_filters/_include/checksum_filter.yaml",
            "root/configuration/http/http_filters/_include/client_cert_filter.yaml",
            "root/configuration/http/http_filters/_include/dns-cache-circuit-breaker.yaml",
            "root/configuration/http/http_filters/_include/geoip-filter.yaml",
            "root/configuration/listeners/network_filters/_include/geoip-network-filter.yaml",
            "root/configuration/http/http_filters/_include/http-cache-configuration-fs.yaml",
            "root/configuration/http/http_filters/_include/http-cache-v2-configuration-fs.yaml",
            "root/configuration/other_features/_include/hyperscan_matcher.yaml",
            "root/configuration/other_features/_include/hyperscan_matcher_multiple.yaml",
            "root/configuration/other_features/_include/hyperscan_regex_engine.yaml",
            "root/configuration/other_features/_include/qatzip.yaml",
            "root/configuration/other_features/_include/qatzstd.yaml",
            "root/intro/arch_overview/security/_include/ssl.yaml",
            "root/configuration/listeners/network_filters/_include/generic_proxy_filter.yaml",
            "root/configuration/overview/_include/xds_api/oauth-sds-example.yaml",
            "root/configuration/security/_include/sds-source-example.yaml",
            "root/configuration/http/http_filters/_include/ip-tagging-filter.yaml",
        ],
    ) + select({
        "@envoy//bazel:windows_x86_64": [],
        "//conditions:default": [
            "root/configuration/http/http_filters/_include/dns-cache-circuit-breaker.yaml",
            "root/intro/arch_overview/security/_include/ssl.yaml",
        ],
    }),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "contrib_configs",
    srcs = [
        "root/configuration/http/http_filters/_include/checksum_filter.yaml",
        "root/configuration/http/http_filters/_include/client_cert_filter.yaml",
        "root/configuration/listeners/network_filters/_include/generic_proxy_filter.yaml",
    ] + select({
        "@envoy//bazel:windows_x86_64": [],
        "//conditions:default": glob(["root/_configs/go/*.yaml"]),
    }),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "proto_examples",
    srcs = glob(["root/**/*.pb"]),
    visibility = ["//visibility:public"],
)

genrule(
    name = "extensions_security_rst",
    srcs = [
        "@envoy//source/extensions:extensions_metadata.yaml",
        "@envoy//contrib:extensions_metadata.yaml",
    ],
    outs = ["extensions_security_rst.tar.gz"],
    cmd = """
    $(location //tools:generate_extensions_security_rst) \\
        $(location @envoy//source/extensions:extensions_metadata.yaml) \\
        $(location @envoy//contrib:extensions_metadata.yaml) $@
    """,
    tools = ["//tools:generate_extensions_security_rst"],
)

genrule(
    name = "external_deps_rst",
    outs = ["external_deps_rst.tar.gz"],
    cmd = """
    $(location //tools:generate_external_deps_rst) \
        $(location @envoy//bazel:all_repository_locations) \
        $@
    """,
    tools = [
        "//tools:generate_external_deps_rst",
        "@envoy//bazel:all_repository_locations",
    ],
)

# These `proto_srcs` need to match the srcs in //tools/protodoc:api_v3_protodoc
# TODO: figure a way to automate these queries from the `protodoc_rule`
genquery(
    name = "v3_proto_srcs",
    expression = "labels(srcs, labels(deps, @envoy_api//:v3_protos))",
    scope = ["@envoy_api//:v3_protos"],
)

genquery(
    name = "xds_proto_srcs",
    expression = "labels(srcs, labels(deps, @envoy_api//:xds_protos))",
    scope = ["@envoy_api//:xds_protos"],
)

genrule(
    name = "proto_srcs",
    outs = ["proto_srcs.txt"],
    cmd = """
    cat $(location :v3_proto_srcs) $(location :xds_proto_srcs)  > $@
    """,
    tools = [
        ":v3_proto_srcs",
        ":xds_proto_srcs",
    ],
)

genrule(
    name = "empty_protos_rst",
    srcs = [":empty_extensions.json"],
    outs = ["empty_protos_rst.tar.gz"],
    cmd = """
    $(location //tools/protodoc:generate_empty) \
        $(location empty_extensions.json) $@
    """,
    tools = ["//tools/protodoc:generate_empty"],
)

genrule(
    name = "api_rst",
    srcs = [
        "//tools/protodoc:api_v3_protodoc",
        ":proto_srcs",
    ],
    outs = ["api_rst.tar.gz"],
    cmd = """
    $(location //tools:generate_api_rst) \\
        $(location proto_srcs) $(locations //tools/protodoc:api_v3_protodoc) $@
    """,
    tools = ["//tools:generate_api_rst"],
)

pkg_files(
    name = "sphinx_base",
    srcs = [
        ":conf.py",
        ":redirects.txt",
        ":versions.yaml",
    ],
)

pkg_files(
    name = "sphinx_root",
    srcs = glob([
        "root/**/*.css",
        "root/**/*.html",
        "root/**/*.ico",
        "root/**/*.jpeg",
        "root/**/*.jpg",
        "root/**/*.js",
        "root/**/*.json",
        "root/**/*.pb",
        "root/**/*.png",
        "root/**/*.proto",
        "root/**/*.rst",
        "root/**/*.svg",
        "root/**/*.yaml",
    ]) + ["root/_pygments/style.py"],
    strip_prefix = "/root",
)

pkg_files(
    name = "sphinx_inventories",
    srcs = glob(["inventories/**/*.inv"]),
    strip_prefix = "",
)

genrule(
    name = "version_histories",
    outs = ["version_histories.tar.gz"],
    cmd = """
    $(location //tools:generate_version_histories) \
        --path=$$(dirname $(location @envoy//:VERSION.txt)) \
        --versions=$(location :versions.yaml) \
        $@
    """,
    tools = [
        ":versions.yaml",
        "//tools:generate_version_histories",
        "@envoy//:VERSION.txt",
        "@envoy//changelogs",
        "@envoy//changelogs:changelogs.yaml",
    ],
)

# TODO(phlax): this appears unused, fix or remove
pkg_files(
    name = "google_vrp_config",
    srcs = ["@envoy//configs:google-vrp/envoy-edge.yaml"],
    prefix = "config/best_practices",
    strip_prefix = "/configs",
)

pkg_files(
    name = "repo_configs",
    srcs = ["@envoy//configs:files"],
    prefix = "_configs/repo",
    strip_prefix = "/configs",
)

pkg_filegroup(
    name = "rst_files",
    srcs = [
        ":repo_configs",
        ":sphinx_base",
        ":sphinx_inventories",
        ":sphinx_root",
    ],
)

pkg_tar(
    name = "version_history_rst",
    extension = "tar.gz",
    package_dir = "version_history",
    deps = [":version_histories"],
)

pkg_tar(
    name = "rst",
    srcs = [":rst_files"],
    extension = "tar.gz",
    deps = [
        ":api_rst",
        ":empty_protos_rst",
        ":extensions_security_rst",
        ":external_deps_rst",
        ":version_history_rst",
        "@envoy-examples//:docs",
    ],
)

sphinx_html(
    name = "html_release",
    out = "html_release.tar.gz",
    descriptor_path = "@envoy_api//:v3_proto_set",
    link_suffix_env = _LINK_SUFFIX_ENV,
    # Build sha/tag may be supplied with --@envoy-docs//:build_sha=<sha> and
    # --@envoy-docs//:docs_tag=<tag>. When unset, stamped builds fall back to workspace status
    # (ENVOY_BUILD_SCM_REVISION), BUILD_DOCS_SHA, and BUILD_DOCS_TAG environment variables.
    rst = ":rst",
    sphinx_runner = "//tools:sphinx_runner",
    stamp = 1,
    version_file = "@envoy//:VERSION.txt",
    visibility = ["//visibility:public"],
    volatile_env = "@envoy//bazel:volatile_env",
)

# No git stamping, speeds up local dev switching branches
sphinx_html(
    name = "html",
    out = "html.tar.gz",
    descriptor_path = "@envoy_api//:v3_proto_set",
    link_suffix_env = _LINK_SUFFIX_ENV,
    rst = ":rst",
    sphinx_runner = "//tools:sphinx_runner",
    version_file = "@envoy//:VERSION.txt",
    visibility = ["//visibility:public"],
)

alias(
    name = "envoy-docs",
    actual = ":html_release",
    visibility = ["//visibility:public"],
)

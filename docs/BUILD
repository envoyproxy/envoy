load("@rules_pkg//pkg:mappings.bzl", "pkg_filegroup", "pkg_files")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

exports_files([
    "protodoc_manifest.yaml",
    "v2_mapping.json",
    "empty_extensions.json",
    "redirects.txt",
])

envoy_package()

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
            # TODO(phlax/windows-dev): figure out how to get this working on windows
            #      "Error: unable to read file: /etc/ssl/certs/ca-certificates.crt"
            "root/configuration/http/http_filters/_include/dns-cache-circuit-breaker.yaml",
            "root/configuration/other_features/_include/dlb.yaml",
            "root/configuration/other_features/_include/hyperscan_matcher.yaml",
            "root/configuration/other_features/_include/hyperscan_matcher_multiple.yaml",
            "root/configuration/other_features/_include/hyperscan_regex_engine.yaml",
            "root/configuration/other_features/_include/qatzip.yaml",
            "root/intro/arch_overview/security/_include/ssl.yaml",
            "root/configuration/listeners/network_filters/_include/generic_proxy_filter.yaml",
            "root/configuration/overview/_include/xds_api/oauth-sds-example.yaml",
            "root/configuration/security/_include/sds-source-example.yaml",
        ],
    ) + select({
        "//bazel:windows_x86_64": [],
        "//conditions:default": [
            "root/configuration/http/http_filters/_include/dns-cache-circuit-breaker.yaml",
            "root/intro/arch_overview/security/_include/ssl.yaml",
        ],
    }),
)

filegroup(
    name = "contrib_configs",
    srcs = [
        "root/configuration/http/http_filters/_include/checksum_filter.yaml",
        "root/configuration/listeners/network_filters/_include/generic_proxy_filter.yaml",
    ] + select({
        "//bazel:windows_x86_64": [],
        "//conditions:default": glob(["root/_configs/go/*.yaml"]),
    }),
)

filegroup(
    name = "proto_examples",
    srcs = glob(["root/**/*.pb"]),
)

genrule(
    name = "extensions_security_rst",
    srcs = [
        "//source/extensions:extensions_metadata.yaml",
        "//contrib:extensions_metadata.yaml",
    ],
    outs = ["extensions_security_rst.tar.gz"],
    cmd = """
    $(location //tools/docs:generate_extensions_security_rst) \\
        $(location //source/extensions:extensions_metadata.yaml) \\
        $(location //contrib:extensions_metadata.yaml) $@
    """,
    tools = ["//tools/docs:generate_extensions_security_rst"],
)

genrule(
    name = "external_deps_rst",
    outs = ["external_deps_rst.tar.gz"],
    cmd = """
    $(location //tools/docs:generate_external_deps_rst) \
        $(location //bazel:all_repository_locations) \
        $@
    """,
    tools = [
        "//bazel:all_repository_locations",
        "//tools/docs:generate_external_deps_rst",
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
    $(location //tools/docs:generate_api_rst) \\
        $(location proto_srcs) $(locations //tools/protodoc:api_v3_protodoc) $@
    """,
    tools = ["//tools/docs:generate_api_rst"],
)

pkg_files(
    name = "sphinx_base",
    srcs = [
        ":conf.py",
        ":redirects.txt",
        ":versions.yaml",
    ],
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
        "root/**/*.proto",
        "root/**/*.rst",
        "root/**/*.svg",
        "root/**/*.yaml",
    ]) + ["root/_pygments/style.py"],
    strip_prefix = "/docs/root",
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
    $(location //tools/docs:generate_version_histories) --path=$$(dirname $(location //:VERSION.txt)) $@
    """,
    tools = [
        ":versions.yaml",
        "//:VERSION.txt",
        "//changelogs",
        "//changelogs:sections.yaml",
        "//tools/docs:generate_version_histories",
    ],
)

# TODO(phlax): this appears unused, fix or remove
pkg_files(
    name = "google_vrp_config",
    srcs = ["//configs:google-vrp/envoy-edge.yaml"],
    prefix = "config/best_practices",
    strip_prefix = "/configs",
)

pkg_files(
    name = "repo_configs",
    srcs = ["//configs"],
    prefix = "_configs/repo",
    strip_prefix = "/configs",
)

pkg_files(
    name = "examples_rst",
    srcs = ["//examples:files"],
    prefix = "start/sandboxes/_include",
    strip_prefix = "/examples",
)

pkg_filegroup(
    name = "rst_files",
    srcs = [
        ":examples_rst",
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
    ],
)

# TODO(phlax): Add `envoy_sphinx` rule to encapsulate these and above targets
genrule(
    name = "html_release",
    outs = ["html_release.tar.gz"],
    # BUILD_SHA must be set in release builds
    # The Envoy workspace will provide this on stamped builds. For external builds
    # you must either pass an env var or pass it through the workspace's status.
    cmd = """
    . $(location //bazel:volatile_env) \
    && _BUILD_SHA=$${BUILD_DOCS_SHA:-$${ENVOY_BUILD_SCM_REVISION:-$${{BUILD_SCM_REVISION}}} \
    && $(location //tools/docs:sphinx_runner) \
         $${SPHINX_RUNNER_ARGS:-} \
         --build_sha="$$_BUILD_SHA" \
         --docs_tag="$${BUILD_DOCS_TAG:-}" \
         --version_file=$(location //:VERSION.txt) \
         --descriptor_path=$(location @envoy_api//:v3_proto_set) \
         $(location rst) \
         $@
    """,
    stamp = 1,
    tools = [
        ":rst",
        "//:VERSION.txt",
        "//bazel:volatile_env",
        "//tools/docs:sphinx_runner",
        "@envoy_api//:v3_proto_set",
    ],
)

# No git stamping, speeds up local dev switching branches
genrule(
    name = "html",
    outs = ["html.tar.gz"],
    cmd = """
    $(location //tools/docs:sphinx_runner) \
         $${SPHINX_RUNNER_ARGS:-} \
         --build_sha="$${BUILD_DOCS_SHA:-}" \
         --version_file=$(location //:VERSION.txt) \
         --descriptor_path=$(location @envoy_api//:v3_proto_set) \
         $(location :rst) \
         $@
    """,
    tools = [
        ":rst",
        "//:VERSION.txt",
        "//tools/docs:sphinx_runner",
        "@envoy_api//:v3_proto_set",
    ],
)

alias(
    name = "docs",
    actual = ":html_release",
)

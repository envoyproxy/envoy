load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load("@rules_pkg//pkg:mappings.bzl", "pkg_filegroup", "pkg_files")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")

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
            "root/**/envoy-dynamic-cds-demo.yaml",
            "root/**/envoy-dynamic-lds-demo.yaml",
            "root/**/envoy-dynamic-filesystem-demo.yaml",
            # TODO(phlax/windows-dev): figure out how to get this working on windows
            #      "Error: unable to read file: /etc/ssl/certs/ca-certificates.crt"
            "root/configuration/http/http_filters/_include/dns-cache-circuit-breaker.yaml",
            "root/intro/arch_overview/security/_include/ssl.yaml",
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
    name = "proto_examples",
    srcs = glob(["root/**/*.pb"]),
)

genrule(
    name = "v2_redirects",
    outs = ["v2_redirects.txt"],
    cmd = """
    jq -r 'with_entries(.key |= sub("^envoy/";"api-v3/"))
          | with_entries(.value |= sub("^envoy/";"api-v2/"))
          | to_entries[]
          | "\\(.value)\t\t\\(.key)"' \\
        $(location :v2_mapping.json) > $@
    """,
    tools = [":v2_mapping.json"],
)

genrule(
    name = "redirects",
    outs = ["envoy-redirects.txt"],
    cmd = """
    cat $(location :redirects.txt) > $@ \\
    && cat $(location :v2_redirects) >> $@
    """,
    tools = [
        ":redirects.txt",
        ":v2_redirects",
    ],
)

genrule(
    name = "extensions_security_rst",
    srcs = [
        "//source/extensions:extensions_metadata.yaml",
        "//contrib:extensions_metadata.yaml",
    ],
    outs = ["extensions_security_rst.tar"],
    cmd = """
    $(location //tools/docs:generate_extensions_security_rst) \\
        $(location //source/extensions:extensions_metadata.yaml) \\
        $(location //contrib:extensions_metadata.yaml) $@
    """,
    tools = ["//tools/docs:generate_extensions_security_rst"],
)

genrule(
    name = "external_deps_rst",
    outs = ["external_deps_rst.tar"],
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
    name = "empty_protos_rst",
    srcs = [":empty_extensions.json"],
    outs = ["empty_protos_rst.tar"],
    cmd = """
    $(location //tools/protodoc:generate_empty) \\
        $(location empty_extensions.json) $@
    """,
    tools = ["//tools/protodoc:generate_empty"],
)

genrule(
    name = "api_rst",
    srcs = [
        "//tools/protodoc:api_v3_protodoc",
        ":v3_proto_srcs",
    ],
    outs = ["api_rst.tar"],
    cmd = """
    $(location //tools/docs:generate_api_rst) \\
        $(location v3_proto_srcs) $(locations //tools/protodoc:api_v3_protodoc) $@
    """,
    tools = ["//tools/docs:generate_api_rst"],
)

genrule(
    name = "xds_rst",
    srcs = [
        "//tools/protodoc:xds_protodoc",
        ":xds_proto_srcs",
    ],
    outs = ["xds_rst.tar"],
    cmd = """
    $(location //tools/docs:generate_api_rst) \\
        $(location xds_proto_srcs) $(locations //tools/protodoc:xds_protodoc) $@
    """,
    tools = ["//tools/docs:generate_api_rst"],
)

pkg_files(
    name = "sphinx_base",
    srcs = glob(
        [
            "conf.py",
            "_ext/*",
        ],
    ) + [":redirects"],
    strip_prefix = "/docs",
)

pkg_files(
    name = "sphinx_root",
    srcs = glob(["root/**/*"]),
    strip_prefix = "/docs/root",
)

# TODO(phlax): this appears unused, fix or remove
pkg_files(
    name = "google_vrp_config",
    srcs = ["//configs:google-vrp/envoy-edge.yaml"],
    prefix = "config/best_practices",
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
        ":sphinx_base",
        ":sphinx_root",
    ],
)

pkg_tar(
    name = "rst",
    srcs = [":rst_files"],
    extension = "tar",
    deps = [
        ":api_rst",
        ":empty_protos_rst",
        ":extensions_security_rst",
        ":external_deps_rst",
        ":xds_rst",
    ],
)

genrule(
    name = "html",
    outs = ["html.tar"],
    cmd = """
    $(location //tools/docs:sphinx_runner) \\
         --build_sha="$${BUILD_SHA:-}" \\
         --docs_tag="$${DOCS_TAG:-}" \\
         --version_file=$(location //:VERSION.txt) \\
         --validator_path=$(location //tools/config_validation:validate_fragment) \\
         --descriptor_path=$(location //tools/type_whisperer:all_protos_with_ext_pb_text.pb_text) \\
         $(location rst) \\
         $@
    """,
    exec_tools = [
        "//tools/docs:sphinx_runner",
        ":rst",
        "//tools/config_validation:validate_fragment",
        "//tools/type_whisperer:all_protos_with_ext_pb_text.pb_text",
        "//:VERSION.txt",
    ],
)

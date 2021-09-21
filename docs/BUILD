load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load("@rules_pkg//:mappings.bzl", "pkg_filegroup", "pkg_files")
load("@rules_pkg//:pkg.bzl", "pkg_tar")

licenses(["notice"])  # Apache 2

exports_files([
    "protodoc_manifest.yaml",
    "v2_mapping.json",
    "empty_extensions.json",
    "redirects.txt",
    "VERSION",
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

pkg_tar(
    name = "api_rst",
    srcs = ["//tools/protodoc:api_v3_protodoc"],
    strip_prefix = "/external",
)

genrule(
    name = "generated_rst",
    cmd = "$(location //tools/docs:builder) $(location :api_rst) --out_path $@",
    outs = ["generated_rst.tar"],
    srcs = [":api_rst"],
    tools = ["//tools/docs:builder"],
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
        ":generated_rst.tar",
    ],
)

genrule(
    name = "html",
    outs = ["html.tar"],
    cmd = """
    $(location //tools/docs:sphinx_runner) \\
         --build_sha="$${BUILD_SHA:-}" \\
         --docs_tag="$${DOCS_TAG:-}" \\
         --version_file=$(location //:VERSION) \\
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
        "//:VERSION",
    ],
)

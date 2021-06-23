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

filegroup(
    name = "base_rst_files",
    srcs = glob(
        [
            "conf.py",
            "_ext/*",
        ],
    ),
)

filegroup(
    name = "root_rst_files",
    srcs = glob(["root/**/*"]),
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
    name = "google_vrp_config",
    outs = ["google_vrp_config.tar"],
    cmd = """
    tar cfh $@ --transform='s/configs/configuration\\/best_practices/' \\
        $(location //configs:google-vrp/envoy-edge.yaml)
    """,
    tools = ["//configs:google-vrp/envoy-edge.yaml"],
)

genrule(
    name = "base_rst",
    outs = ["base_rst.tar"],
    cmd = """
    tar cfh $@ --transform='s/docs\\///' $(locations base_rst_files) \\
    && tar rhf $@ --transform='s/docs\\/root\\///' $(locations root_rst_files) \\
    && tar rf $@ -C $$(dirname $(location redirects)) $$(basename $(location redirects))
    """,
    tools = [
        ":base_rst_files",
        ":redirects",
        ":root_rst_files",
    ],
)

genrule(
    name = "examples_rst",
    outs = ["examples_rst.tar"],
    cmd = """
    tar cfh $@ --transform='s/examples/start\\/sandboxes\\/_include/' \\
        $(locations //examples:files)
    """,
    tools = ["//examples:files"],
)

genrule(
    name = "extensions_security_rst",
    srcs = ["//source/extensions:extensions_metadata.yaml"],
    outs = ["extensions_security_rst.tar"],
    cmd = """
    $(location //tools/docs:generate_extensions_security_rst) \\
        $(location //source/extensions:extensions_metadata.yaml) $@
    """,
    tools = ["//tools/docs:generate_extensions_security_rst"],
)

genrule(
    name = "external_deps_rst",
    srcs = [
        "//bazel:repository_locations.bzl",
        "@envoy_api_canonical//bazel:repository_locations.bzl",
        "@envoy_api_canonical//bazel:repository_locations_utils.bzl",
    ],
    outs = ["external_deps_rst.tar"],
    cmd = "$(location //tools/docs:generate_external_deps_rst) $@",
    tools = ["//tools/docs:generate_external_deps_rst"],
)

genquery(
    name = "v3_proto_srcs",
    expression = "labels(srcs, labels(deps, @envoy_api_canonical//:v3_protos))",
    scope = ["@envoy_api_canonical//:v3_protos"],
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
    name = "rst",
    outs = ["rst.tar"],
    cmd = """
    cat $(location base_rst) > $@ \\
    && tar -Af $@ $(location api_rst) \\
    && tar -Af $@ $(location examples_rst) \\
    && tar -Af $@ $(location extensions_security_rst) \\
    && tar -Af $@ $(location external_deps_rst) \\
    && tar -Af $@ $(location empty_protos_rst)
    """,
    tools = [
        ":api_rst",
        ":base_rst",
        ":empty_protos_rst",
        ":examples_rst",
        ":extensions_security_rst",
        ":external_deps_rst",
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

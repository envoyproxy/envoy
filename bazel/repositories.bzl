load("@com_google_googleapis//:repository_rules.bzl", "switched_rules_by_language")
load("@envoy_api//bazel:envoy_http_archive.bzl", "envoy_http_archive")
load("@envoy_api//bazel:external_deps.bzl", "load_repository_locations")
load(":repository_locations.bzl", "PROTOC_VERSIONS", "REPOSITORY_LOCATIONS_SPEC")

PPC_SKIP_TARGETS = ["envoy.string_matcher.lua", "envoy.filters.http.lua", "envoy.router.cluster_specifier_plugin.lua"]

WINDOWS_SKIP_TARGETS = [
    "envoy.extensions.http.cache.file_system_http_cache",
    "envoy.extensions.http.cache_v2.file_system_http_cache",
    "envoy.filters.http.file_system_buffer",
    "envoy.filters.http.language",
    "envoy.filters.http.sxg",
    "envoy.tracers.dynamic_ot",
    "envoy.tracers.datadog",
    # Extensions that require CEL.
    "envoy.access_loggers.extension_filters.cel",
    "envoy.rate_limit_descriptors.expr",
    "envoy.filters.http.rate_limit_quota",
    "envoy.filters.http.ext_proc",
    "envoy.formatter.cel",
    "envoy.matching.inputs.cel_data_input",
    "envoy.matching.matchers.cel_matcher",
    # Wasm and RBAC extensions have a link dependency on CEL.
    "envoy.access_loggers.wasm",
    "envoy.bootstrap.wasm",
    "envoy.filters.http.wasm",
    "envoy.filters.network.wasm",
    "envoy.stat_sinks.wasm",
    # RBAC extensions have a link dependency on CEL.
    "envoy.filters.http.rbac",
    "envoy.filters.network.rbac",
    "envoy.rbac.matchers.upstream_ip_port",
]

NO_HTTP3_SKIP_TARGETS = [
    "envoy.quic.crypto_stream.server.quiche",
    "envoy.quic.deterministic_connection_id_generator",
    "envoy.quic.proof_source.filter_chain",
    "envoy.quic.server_preferred_address.fixed",
    "envoy.quic.server_preferred_address.datasource",
    "envoy.quic.connection_debug_visitor.basic",
    "envoy.quic.packet_writer.default",
]

# Make all contents of an external repository accessible under a filegroup.  Used for external HTTP
# archives, e.g. cares.
def _build_all_content(exclude = []):
    return """filegroup(name = "all", srcs = glob(["**"], exclude={}), visibility = ["//visibility:public"])""".format(repr(exclude))

BUILD_ALL_CONTENT = _build_all_content()

REPOSITORY_LOCATIONS = load_repository_locations(REPOSITORY_LOCATIONS_SPEC)

# Use this macro to reference any HTTP archive from bazel/repository_locations.bzl.
def external_http_archive(name, **kwargs):
    envoy_http_archive(
        name,
        locations = REPOSITORY_LOCATIONS,
        **kwargs
    )

def _default_envoy_build_config_impl(ctx):
    ctx.file("WORKSPACE", "")
    ctx.file("BUILD.bazel", "")
    ctx.symlink(ctx.attr.config, "extensions_build_config.bzl")

default_envoy_build_config = repository_rule(
    implementation = _default_envoy_build_config_impl,
    attrs = {
        "config": attr.label(default = "@envoy//source/extensions:extensions_build_config.bzl"),
    },
)

def _go_deps(skip_targets):
    # Keep the skip_targets check around until Istio Proxy has stopped using
    # it to exclude the Go rules.
    if "io_bazel_rules_go" not in skip_targets:
        external_http_archive(name = "io_bazel_rules_go")
        external_http_archive("bazel_gazelle")

def _rust_deps():
    external_http_archive(
        "rules_rust",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:rules_rust.patch"],
    )

def envoy_dependencies(skip_targets = [], bzlmod = False):
    """Load Envoy dependencies.

    This function loads all Envoy dependencies for both WORKSPACE and bzlmod modes.
    When bzlmod=True, dependencies already available in Bazel Central Registry (BCR)
    are skipped since they're loaded via bazel_dep() in MODULE.bazel.

    Args:
        skip_targets: List of targets to skip (legacy WORKSPACE parameter)
        bzlmod: If True, skip dependencies already in BCR (loaded via bazel_dep)
    """

    # Treat Envoy's overall build config as an external repo, so projects that
    # build Envoy as a subcomponent can easily override the config.
    if "envoy_build_config" not in native.existing_rules().keys():
        default_envoy_build_config(name = "envoy_build_config")

    _aws_lc()  # Not in BCR, needed for both modes

    # The long repo names (`com_github_fmtlib_fmt` instead of `fmtlib`) are
    # semi-standard in the Bazel community, intended to avoid both duplicate
    # dependencies and name conflicts.
    _com_github_envoyproxy_sqlparser()
    _com_github_google_jwt_verify()
    _com_github_google_quiche()
    external_http_archive("bazel_compdb")

    _thrift()

    # Protoc binaries for different platforms - needed for both modes
    for platform in PROTOC_VERSIONS:
        external_http_archive(
            "com_google_protobuf_protoc_%s" % platform,
            build_file = "@envoy//bazel/protoc:BUILD.protoc",
        )

    _kafka_deps()

def _aws_lc():
    external_http_archive(
        name = "aws_lc",
        build_file = "@envoy//bazel/external:aws_lc.BUILD",
    )

def _com_github_bazel_buildtools():
    # TODO(phlax): Add binary download
    #  cf: https://github.com/bazelbuild/buildtools/issues/367
    external_http_archive(
        name = "com_github_bazelbuild_buildtools",
    )

def _com_github_cyan4973_xxhash():
    external_http_archive(
        name = "com_github_cyan4973_xxhash",
        build_file = "@envoy//bazel/external:xxhash.BUILD",
    )

def _com_github_envoyproxy_sqlparser():
    external_http_archive(
        name = "com_github_envoyproxy_sqlparser",
        build_file = "@envoy//bazel/external:sqlparser.BUILD",
    )

def _com_github_fmtlib_fmt():
    external_http_archive(
        name = "com_github_fmtlib_fmt",
        build_file = "@envoy//bazel/external:fmtlib.BUILD",
    )

def _com_github_gabime_spdlog():
    external_http_archive(
        name = "com_github_gabime_spdlog",
        build_file = "@envoy//bazel/external:spdlog.BUILD",
    )

def _com_github_google_benchmark():
    external_http_archive(
        name = "com_github_google_benchmark",
    )
    external_http_archive(
        name = "libpfm",
        build_file = "@com_github_google_benchmark//tools:libpfm.BUILD.bazel",
    )

def _numactl():
    external_http_archive(
        name = "numactl",
        build_file = "@envoy//bazel/external:numactl.BUILD",
    )

def _com_github_jbeder_yaml_cpp():
    external_http_archive(
        name = "com_github_jbeder_yaml_cpp",
    )

# If you're looking for envoy-filter-example / envoy_filter_example
# the hash is in ci/filter_example_setup.sh

def _zstd():
    external_http_archive(
        name = "zstd",
        build_file = "@envoy//bazel/external:zstd.BUILD",
    )

def _com_github_nlohmann_json():
    external_http_archive(
        name = "com_github_nlohmann_json",
    )

def _com_google_googletest():
    external_http_archive(
        "com_google_googletest",
        patches = ["@envoy//bazel:googletest.patch"],
        patch_args = ["-p1"],
        repo_mapping = {
            "@abseil-cpp": "@com_google_absl",
            "@re2": "@com_googlesource_code_re2",
        },
    )

# TODO(jmarantz): replace the use of bind and external_deps with just
# the direct Bazel path at all sites.  This will make it easier to
# pull in more bits of abseil as needed, and is now the preferred
# method for pure Bazel deps.
def _com_google_absl():
    external_http_archive(
        name = "com_google_absl",
        patches = ["@envoy//bazel:abseil.patch"],
        patch_args = ["-p1"],
        repo_mapping = {"@googletest": "@com_google_googletest"},
    )

def _com_google_protobuf():
    external_http_archive(
        name = "rules_python",
    )
    external_http_archive(
        name = "rules_java",
        patch_args = ["-p1"],
        patches = [
            "@envoy//bazel:rules_java.patch",
        ],
    )

    for platform in PROTOC_VERSIONS:
        # Ideally we dont use a private build artefact as done here.
        # If `rules_proto` implements protoc toolchains in the future (currently it
        # is there, but is empty) we should remove these and use that rule
        # instead.
        external_http_archive(
            "com_google_protobuf_protoc_%s" % platform,
            build_file = "@envoy//bazel/protoc:BUILD.protoc",
        )

    external_http_archive(
        "com_google_protobuf",
        patches = ["@envoy//bazel:protobuf.patch"],
        patch_args = ["-p1"],
        repo_mapping = {
            "@abseil-cpp": "@com_google_absl",
        },
    )

def _com_github_google_quiche():
    external_http_archive(
        name = "com_github_google_quiche",
        patch_cmds = ["find quiche/ -type f -name \"*.bazel\" -delete"],
        build_file = "@envoy//bazel/external:quiche.BUILD",
    )

def _rules_proto_grpc():
    external_http_archive("rules_proto_grpc")

def _re2():
    external_http_archive(
        "com_googlesource_code_re2",
        repo_mapping = {"@abseil-cpp": "@com_google_absl"},
    )

def _proxy_wasm_cpp_sdk():
    external_http_archive(
        name = "proxy_wasm_cpp_sdk",
        patch_args = ["-p1"],
        patches = [
            "@envoy//bazel:proxy_wasm_cpp_sdk.patch",
        ],
    )

def _proxy_wasm_cpp_host():
    external_http_archive(
        name = "proxy_wasm_cpp_host",
        patch_args = ["-p1"],
        patches = [
            "@envoy//bazel:proxy_wasm_cpp_host.patch",
        ],
    )

def _emsdk():
    external_http_archive(
        name = "emsdk",
        patch_args = ["-p2"],
        patches = ["@envoy//bazel:emsdk.patch"],
    )

def _com_github_google_jwt_verify():
    external_http_archive(
        "com_github_google_jwt_verify",
        patches = ["@envoy//bazel:jwt_verify_lib.patch"],
        patch_args = ["-p1"],
    )

def _gperftools():
    external_http_archive(
        name = "gperftools",
    )

def _toolchains_llvm():
    external_http_archive(name = "toolchains_llvm")

def _rules_fuzzing():
    external_http_archive(
        name = "rules_fuzzing",
        repo_mapping = {
            "@fuzzing_py_deps": "@fuzzing_pip3",
        },
        # TODO(asraa): Try this fix for OSS-Fuzz build failure on tar command.
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:rules_fuzzing.patch"],
    )

def _kafka_deps():
    # This archive contains Kafka client source code.
    # We are using request/response message format files to generate parser code.
    KAFKASOURCE_BUILD_CONTENT = """
filegroup(
    name = "request_protocol_files",
    srcs = glob(["*Request.json"]),
    visibility = ["//visibility:public"],
)
filegroup(
    name = "response_protocol_files",
    srcs = glob(["*Response.json"]),
    visibility = ["//visibility:public"],
)
    """
    external_http_archive(
        name = "kafka_source",
        build_file_content = KAFKASOURCE_BUILD_CONTENT,
    )

    # This archive provides Kafka (and Zookeeper) binaries, that are used during Kafka integration
    # tests.
    external_http_archive(
        name = "kafka_server_binary",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _rules_ruby():
    external_http_archive("rules_ruby")

def _foreign_cc_dependencies():
    external_http_archive(
        name = "rules_foreign_cc",
        patches = ["@envoy//bazel:rules_foreign_cc.patch"],
        patch_args = ["-p1"],
    )

def _thrift():
    external_http_archive(
        name = "thrift",
        build_file = "@envoy//bazel/external:thrift.BUILD",
        patches = ["@envoy//bazel:thrift.patch"],
        patch_args = ["-p1"],
        patch_cmds = ["mv src thrift"],
    )

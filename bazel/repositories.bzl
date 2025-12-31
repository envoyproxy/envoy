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

# Bazel native C++ dependencies. For the dependencies that doesn't provide autoconf/automake builds.
def _cc_deps():
    external_http_archive(
        name = "grpc_httpjson_transcoding",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:grpc_httpjson_transcoding.patch"],
    )
    external_http_archive(
        name = "com_google_protoconverter",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:com_google_protoconverter.patch"],
        patch_cmds = [
            "rm src/google/protobuf/stubs/common.cc",
            "rm src/google/protobuf/stubs/common.h",
            "rm src/google/protobuf/stubs/common_unittest.cc",
            "rm src/google/protobuf/util/converter/port_def.inc",
            "rm src/google/protobuf/util/converter/port_undef.inc",
        ],
    )
    external_http_archive("com_google_protofieldextraction")
    external_http_archive(
        "com_google_protoprocessinglib",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:proto_processing_lib.patch"],
    )
    external_http_archive("ocp")

def _go_deps(skip_targets):
    # Keep the skip_targets check around until Istio Proxy has stopped using
    # it to exclude the Go rules.
    if "io_bazel_rules_go" not in skip_targets:
        external_http_archive(name = "io_bazel_rules_go")
        external_http_archive("bazel_gazelle")

def _rust_deps():
    external_http_archive(
        "rules_rust",
        patch_args = ["-p0"],
        patches = ["@envoy//bazel:rules_rust.patch"],
    )

def envoy_dependencies(skip_targets = []):
    external_http_archive("platforms")

    # Treat Envoy's overall build config as an external repo, so projects that
    # build Envoy as a subcomponent can easily override the config.
    if "envoy_build_config" not in native.existing_rules().keys():
        default_envoy_build_config(name = "envoy_build_config")

    # Setup Bazel shell rules
    external_http_archive(name = "rules_shell")

    # Setup Bazel C++ rules
    external_http_archive("rules_cc")

    # Setup external Bazel rules
    _foreign_cc_dependencies()

    # BoringSSL:
    # - BoringSSL FIPS from @boringssl_fips//:ssl,
    # - non-FIPS BoringSSL from @boringssl//:ssl.
    # SSL/crypto dependencies are resolved via EXTERNAL_DEPS_MAP in envoy_internal.bzl
    _boringssl()
    _boringssl_fips()
    _aws_lc()

    # The long repo names (`com_github_fmtlib_fmt` instead of `fmtlib`) are
    # semi-standard in the Bazel community, intended to avoid both duplicate
    # dependencies and name conflicts.
    _com_github_awslabs_aws_c_auth()
    _com_github_axboe_liburing()
    _com_github_bazel_buildtools()
    _com_github_c_ares_c_ares()
    _com_github_openhistogram_libcircllhist()
    _com_github_cyan4973_xxhash()
    _com_github_datadog_dd_trace_cpp()
    _com_github_mirror_tclap()
    _com_github_envoyproxy_sqlparser()
    _com_github_fmtlib_fmt()
    _com_github_gabime_spdlog()
    _com_github_google_benchmark()
    _com_github_google_jwt_verify()
    _com_github_google_libprotobuf_mutator()
    _com_github_google_libsxg()
    _com_github_google_tcmalloc()
    _gperftools()
    _com_github_grpc_grpc()
    _rules_proto_grpc()
    _com_github_unicode_org_icu()
    _com_github_intel_ipp_crypto_crypto_mb()
    _numactl()
    _uadk()
    _com_github_intel_qatlib()
    _com_github_intel_qatzip()
    _com_github_qat_zstd()
    _com_github_lz4_lz4()
    _com_github_jbeder_yaml_cpp()
    _com_github_libevent_libevent()
    _com_github_luajit_luajit()
    _com_github_nghttp2_nghttp2()
    _com_github_msgpack_cpp()
    _com_github_skyapm_cpp2sky()
    _com_github_alibaba_hessian2_codec()
    _com_github_nlohmann_json()
    _com_github_ncopa_suexec()
    _com_google_absl()
    _com_google_googletest()
    _com_google_protobuf()
    _v8()
    _fast_float()
    _highway()
    _dragonbox()
    _fp16()
    _simdutf()
    _intel_ittapi()
    _com_github_google_quiche()
    _googleurl()
    _io_hyperscan()
    _io_vectorscan()
    _io_opentelemetry_api_cpp()
    _net_colm_open_source_colm()
    _net_colm_open_source_ragel()
    _zlib()
    _intel_dlb()
    _com_github_zlib_ng_zlib_ng()
    _org_boost()
    _org_brotli()
    _zstd()
    _re2()
    _proxy_wasm_cpp_sdk()
    _proxy_wasm_cpp_host()
    _emsdk()
    _rules_fuzzing()
    external_http_archive("proxy_wasm_rust_sdk")
    _com_google_cel_cpp()
    _com_github_google_perfetto()
    _rules_ruby()
    external_http_archive("com_github_google_flatbuffers")
    external_http_archive("bazel_features")
    external_http_archive("bazel_toolchains")
    external_http_archive("bazel_compdb")
    external_http_archive(
        name = "envoy_examples",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:envoy_examples.patch"],
    )
    external_http_archive("envoy_toolshed")

    _com_github_maxmind_libmaxminddb()

    external_http_archive("rules_license")
    external_http_archive("rules_pkg")
    external_http_archive("com_github_aignas_rules_shellcheck")
    external_http_archive(
        "aspect_bazel_lib",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:aspect.patch"],
    )

    _com_github_fdio_vpp_vcl()

    # Unconditional, since we use this only for compiler-agnostic fuzzing utils.
    _org_llvm_releases_compiler_rt()

    _toolchains_llvm()

    _cc_deps()
    _go_deps(skip_targets)
    _rust_deps()
    _kafka_deps()
    _com_github_wamr()
    _com_github_wasmtime()

    switched_rules_by_language(
        name = "com_google_googleapis_imports",
        cc = True,
        go = True,
        python = True,
        grpc = True,
    )

def _boringssl():
    external_http_archive(name = "boringssl")

def _boringssl_fips():
    external_http_archive(
        name = "boringssl_fips",
        location_name = "boringssl",
        build_file = "@envoy//bazel/external:boringssl_fips.BUILD",
        patches = ["@envoy//bazel:boringssl_fips.patch"],
        patch_args = ["-p1"],
    )

    NINJA_BUILD_CONTENT = "%s\nexports_files([\"configure.py\"])" % BUILD_ALL_CONTENT
    external_http_archive(
        name = "fips_ninja",
        build_file_content = NINJA_BUILD_CONTENT,
    )
    CMAKE_BUILD_CONTENT = "%s\nexports_files([\"bin/cmake\"])" % BUILD_ALL_CONTENT
    external_http_archive(
        name = "fips_cmake_linux_x86_64",
        build_file_content = CMAKE_BUILD_CONTENT,
    )
    external_http_archive(
        name = "fips_cmake_linux_aarch64",
        build_file_content = CMAKE_BUILD_CONTENT,
    )
    GO_BUILD_CONTENT = "%s\nexports_files([\"bin/go\"])" % _build_all_content(["test/**"])
    external_http_archive(
        name = "fips_go_linux_amd64",
        build_file_content = GO_BUILD_CONTENT,
    )
    external_http_archive(
        name = "fips_go_linux_arm64",
        build_file_content = GO_BUILD_CONTENT,
    )

def _aws_lc():
    external_http_archive(
        name = "aws_lc",
        build_file = "@envoy//bazel/external:aws_lc.BUILD",
    )

def _com_github_openhistogram_libcircllhist():
    external_http_archive(
        name = "com_github_openhistogram_libcircllhist",
        build_file = "@envoy//bazel/external:libcircllhist.BUILD",
    )

def _com_github_awslabs_aws_c_auth():
    external_http_archive(
        name = "com_github_awslabs_aws_c_auth",
        build_file = "@envoy//bazel/external:aws-c-auth.BUILD",
    )

def _com_github_axboe_liburing():
    external_http_archive(
        name = "com_github_axboe_liburing",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:liburing.patch"],
    )

def _com_github_bazel_buildtools():
    # TODO(phlax): Add binary download
    #  cf: https://github.com/bazelbuild/buildtools/issues/367
    external_http_archive(
        name = "com_github_bazelbuild_buildtools",
    )

def _com_github_c_ares_c_ares():
    external_http_archive(
        name = "com_github_c_ares_c_ares",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:c-ares.patch"],
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

def _com_github_mirror_tclap():
    external_http_archive(
        name = "com_github_mirror_tclap",
        build_file = "@envoy//bazel/external:tclap.BUILD",
        patch_args = ["-p1"],
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

def _com_github_google_libprotobuf_mutator():
    external_http_archive(
        name = "com_github_google_libprotobuf_mutator",
        build_file = "@envoy//bazel/external:libprotobuf_mutator.BUILD",
    )

def _com_github_google_libsxg():
    external_http_archive(
        name = "com_github_google_libsxg",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _com_github_unicode_org_icu():
    external_http_archive(
        name = "com_github_unicode_org_icu",
        patches = ["@envoy//bazel/foreign_cc:icu.patch"],
        patch_args = ["-p1"],
        build_file_content = BUILD_ALL_CONTENT,
    )

def _com_github_intel_ipp_crypto_crypto_mb():
    external_http_archive(
        name = "com_github_intel_ipp_crypto_crypto_mb",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _numactl():
    external_http_archive(
        name = "numactl",
        build_file = "@envoy//bazel/external:numactl.BUILD",
    )

def _uadk():
    external_http_archive(
        name = "uadk",
        patches = ["@envoy//bazel/foreign_cc:uadk.patch"],
        patch_args = ["-p1"],
        build_file_content = BUILD_ALL_CONTENT,
    )

def _com_github_intel_qatlib():
    external_http_archive(
        name = "com_github_intel_qatlib",
        build_file_content = BUILD_ALL_CONTENT,
        patches = ["@envoy//bazel/foreign_cc:qatlib.patch"],
        patch_args = ["-p1"],
    )

def _com_github_intel_qatzip():
    external_http_archive(
        name = "com_github_intel_qatzip",
        build_file_content = BUILD_ALL_CONTENT,
        patches = ["@envoy//bazel/foreign_cc:qatzip.patch"],
        patch_args = ["-p1"],
    )

def _com_github_qat_zstd():
    external_http_archive(
        name = "com_github_qat_zstd",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:qatzstd.patch"],
    )

def _com_github_lz4_lz4():
    external_http_archive(
        name = "com_github_lz4_lz4",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _com_github_jbeder_yaml_cpp():
    external_http_archive(
        name = "com_github_jbeder_yaml_cpp",
    )

def _com_github_libevent_libevent():
    external_http_archive(
        name = "com_github_libevent_libevent",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _net_colm_open_source_colm():
    external_http_archive(
        name = "net_colm_open_source_colm",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _net_colm_open_source_ragel():
    external_http_archive(
        name = "net_colm_open_source_ragel",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _zlib():
    external_http_archive(
        name = "zlib",
        build_file = "@envoy//bazel/external:zlib.BUILD",
    )

def _com_github_zlib_ng_zlib_ng():
    external_http_archive(
        name = "com_github_zlib_ng_zlib_ng",
        build_file_content = BUILD_ALL_CONTENT,
    )

# Boost in general is not approved for Envoy use, and the header-only
# dependency is only for the Hyperscan contrib package.
def _org_boost():
    external_http_archive(
        name = "org_boost",
        build_file_content = """
filegroup(
    name = "header",
    srcs = glob([
        "boost/**/*.h",
        "boost/**/*.hpp",
        "boost/**/*.ipp",
    ]),
    visibility = ["@envoy//contrib/hyperscan/matching/input_matchers/source:__pkg__"],
)
""",
    )

# If you're looking for envoy-filter-example / envoy_filter_example
# the hash is in ci/filter_example_setup.sh

def _org_brotli():
    external_http_archive(
        name = "org_brotli",
    )

def _zstd():
    external_http_archive(
        name = "zstd",
        build_file = "@envoy//bazel/external:zstd.BUILD",
    )

def _com_google_cel_cpp():
    external_http_archive(
        name = "com_google_cel_cpp",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:cel-cpp.patch"],
    )

    # Load required dependencies that cel-cpp expects.
    external_http_archive("com_google_cel_spec")

    # cel-cpp references ``@antlr4-cpp-runtime//:antlr4-cpp-runtime`` but it internally
    # defines ``antlr4_runtimes`` with a cpp target.
    # We are creating a repository alias to avoid duplicating the ANTLR4 dependency.
    native.new_local_repository(
        name = "antlr4-cpp-runtime",
        path = ".",
        build_file_content = """
package(default_visibility = ["//visibility:public"])

# Alias to cel-cpp's embedded ANTLR4 runtime.
alias(
    name = "antlr4-cpp-runtime",
    actual = "@antlr4_runtimes//:cpp",
)
""",
    )

def _com_github_google_perfetto():
    external_http_archive(
        name = "com_github_google_perfetto",
        build_file_content = """
package(default_visibility = ["//visibility:public"])
cc_library(
    name = "perfetto",
    srcs = ["perfetto.cc"],
    hdrs = ["perfetto.h"],
)
""",
    )

def _com_github_nghttp2_nghttp2():
    external_http_archive(
        name = "com_github_nghttp2_nghttp2",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p1"],
        # This patch cannot be picked up due to ABI rules. Discussion at;
        # https://github.com/nghttp2/nghttp2/pull/1395
        # https://github.com/envoyproxy/envoy/pull/8572#discussion_r334067786
        patches = [
            "@envoy//bazel/foreign_cc:nghttp2.patch",
            "@envoy//bazel/foreign_cc:nghttp2_huffman.patch",
        ],
    )

def _com_github_msgpack_cpp():
    external_http_archive(
        name = "com_github_msgpack_cpp",
        build_file = "@envoy//bazel/external:msgpack.BUILD",
    )

def _io_hyperscan():
    external_http_archive(
        name = "io_hyperscan",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:hyperscan.patch"],
    )

def _io_vectorscan():
    external_http_archive(
        name = "io_vectorscan",
        build_file_content = BUILD_ALL_CONTENT,
        type = "tar.gz",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:vectorscan.patch"],
    )

def _io_opentelemetry_api_cpp():
    external_http_archive(
        name = "io_opentelemetry_cpp",
    )

def _com_github_datadog_dd_trace_cpp():
    external_http_archive("com_github_datadog_dd_trace_cpp")

def _com_github_skyapm_cpp2sky():
    external_http_archive(
        name = "com_github_skyapm_cpp2sky",
        patches = ["@envoy//bazel:com_github_skyapm_cpp2sky.patch"],
        patch_args = ["-p1"],
    )
    external_http_archive(
        name = "skywalking_data_collect_protocol",
    )

def _com_github_nlohmann_json():
    external_http_archive(
        name = "com_github_nlohmann_json",
    )

def _com_github_alibaba_hessian2_codec():
    external_http_archive("com_github_alibaba_hessian2_codec")

def _com_github_ncopa_suexec():
    external_http_archive(
        name = "com_github_ncopa_suexec",
        build_file = "@envoy//bazel/external:su-exec.BUILD",
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

def _v8():
    external_http_archive(
        name = "v8",
        patches = [
            "@envoy//bazel:v8.patch",
            "@envoy//bazel:v8_ppc64le.patch",
            # https://issues.chromium.org/issues/423403090
            "@envoy//bazel:v8_python.patch",
        ],
        patch_args = ["-p1"],
        patch_cmds = [
            "find ./src ./include -type f -exec sed -i.bak -e 's!#include \"third_party/simdutf/simdutf.h\"!#include \"simdutf.h\"!' {} \\;",
            "find ./src ./include -type f -exec sed -i.bak -e 's!#include \"third_party/fp16/src/include/fp16.h\"!#include \"fp16.h\"!' {} \\;",
            "find ./src ./include -type f -exec sed -i.bak -e 's!#include \"third_party/dragonbox/src/include/dragonbox/dragonbox.h\"!#include \"dragonbox/dragonbox.h\"!' {} \\;",
            "find ./src ./include -type f -exec sed -i.bak -e 's!#include \"third_party/fast_float/src/include/fast_float/!#include \"fast_float/!' {} \\;",
        ],
        repo_mapping = {
            "@abseil-cpp": "@com_google_absl",
            "@icu": "@com_github_unicode_org_icu",
        },
    )

def _fast_float():
    external_http_archive(
        name = "fast_float",
    )

def _highway():
    external_http_archive(
        name = "highway",
        patches = [
            "@envoy//bazel:highway-ppc64le.patch",
        ],
        patch_args = ["-p1"],
    )

def _dragonbox():
    external_http_archive(
        name = "dragonbox",
        build_file = "@envoy//bazel/external:dragonbox.BUILD",
    )

def _fp16():
    external_http_archive(
        name = "fp16",
        build_file = "@envoy//bazel/external:fp16.BUILD",
    )

def _simdutf():
    external_http_archive(
        name = "simdutf",
        build_file = "@envoy//bazel/external:simdutf.BUILD",
    )

def _intel_ittapi():
    external_http_archive(
        name = "intel_ittapi",
        build_file = "@envoy//bazel/external:intel_ittapi.BUILD",
    )

def _com_github_google_quiche():
    external_http_archive(
        name = "com_github_google_quiche",
        patch_cmds = ["find quiche/ -type f -name \"*.bazel\" -delete"],
        build_file = "@envoy//bazel/external:quiche.BUILD",
    )

def _googleurl():
    external_http_archive(
        name = "googleurl",
        patches = ["@envoy//bazel/external:googleurl.patch"],
        patch_args = ["-p1"],
    )

def _org_llvm_releases_compiler_rt():
    external_http_archive(
        name = "org_llvm_releases_compiler_rt",
        build_file = "@envoy//bazel/external:compiler_rt.BUILD",
    )

def _com_github_grpc_grpc():
    external_http_archive(
        name = "com_github_grpc_grpc",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:grpc.patch"],
        repo_mapping = {"@openssl": "@boringssl"},
    )
    external_http_archive(
        "build_bazel_rules_apple",
        patch_args = ["-p1"],
        patches = [
            "@envoy//bazel:rules_apple.patch",
            "@envoy//bazel:rules_apple_py.patch",
        ],
    )

def _rules_proto_grpc():
    external_http_archive("rules_proto_grpc")

def _re2():
    external_http_archive("com_googlesource_code_re2")

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

def _com_github_luajit_luajit():
    external_http_archive(
        name = "com_github_luajit_luajit",
        build_file_content = BUILD_ALL_CONTENT,
        patches = ["@envoy//bazel/foreign_cc:luajit.patch"],
        patch_args = ["-p1"],
    )

def _com_github_google_tcmalloc():
    external_http_archive(
        name = "com_github_google_tcmalloc",
        patches = ["@envoy//bazel:tcmalloc.patch"],
        patch_args = ["-p1"],
    )

def _gperftools():
    external_http_archive(
        name = "gperftools",
    )

def _com_github_wamr():
    external_http_archive(
        name = "com_github_wamr",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _toolchains_llvm():
    external_http_archive(name = "toolchains_llvm")

def _com_github_wasmtime():
    external_http_archive(
        name = "com_github_wasmtime",
        build_file = "@proxy_wasm_cpp_host//:bazel/external/wasmtime.BUILD",
    )

def _intel_dlb():
    external_http_archive(
        name = "intel_dlb",
        build_file_content = """
filegroup(
    name = "libdlb",
    srcs = glob(["dlb/libdlb/*"]),
    visibility = ["@envoy//contrib/dlb/source:__pkg__"],
)
""",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:dlb.patch"],
        patch_cmds = ["cp dlb/driver/dlb2/uapi/linux/dlb2_user.h dlb/libdlb/"],
    )

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

    # This archive provides Kafka C/CPP client used by mesh filter to communicate with upstream
    # Kafka clusters.
    external_http_archive(
        name = "confluentinc_librdkafka",
        build_file_content = BUILD_ALL_CONTENT,
        # (adam.kotwasinski) librdkafka bundles in cJSON, which is also bundled in by libvppinfra.
        # For now, let's just drop this dependency from Kafka, as it's used only for monitoring.
        patches = ["@envoy//bazel/foreign_cc:librdkafka.patch"],
        patch_args = ["-p1"],
    )

    # This archive provides Kafka (and Zookeeper) binaries, that are used during Kafka integration
    # tests.
    external_http_archive(
        name = "kafka_server_binary",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _com_github_fdio_vpp_vcl():
    external_http_archive(
        name = "com_github_fdio_vpp_vcl",
        build_file_content = _build_all_content(exclude = ["**/*doc*/**", "**/examples/**", "**/plugins/**"]),
        patches = ["@envoy//bazel/foreign_cc:vpp_vcl.patch"],
        patch_args = ["-p1"],
    )

def _rules_ruby():
    external_http_archive("rules_ruby")

def _foreign_cc_dependencies():
    external_http_archive(
        name = "rules_foreign_cc",
        patches = ["@envoy//bazel:rules_foreign_cc.patch"],
        patch_args = ["-p1"],
    )

def _com_github_maxmind_libmaxminddb():
    external_http_archive(
        name = "com_github_maxmind_libmaxminddb",
        build_file_content = BUILD_ALL_CONTENT,
    )

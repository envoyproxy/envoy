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

# Bazel native C++ dependencies. For the dependencies that doesn't provide autoconf/automake builds.
def _cc_deps():
    external_http_archive(
        name = "grpc_httpjson_transcoding",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:grpc_httpjson_transcoding.patch"],
        repo_mapping = {
            "@com_google_absl": "@abseil-cpp",
            "@com_google_protoconverter": "@proto-converter",
        },
    )
    external_http_archive(
        "proto-converter",
        location_name = "proto_converter",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:com_google_protoconverter.patch"],
        patch_cmds = [
            "rm src/google/protobuf/stubs/common.cc",
            "rm src/google/protobuf/stubs/common.h",
            "rm src/google/protobuf/stubs/common_unittest.cc",
            "rm src/google/protobuf/util/converter/port_def.inc",
            "rm src/google/protobuf/util/converter/port_undef.inc",
        ],
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
    )
    external_http_archive(
        "proto-field-extraction",
        location_name = "proto_field_extraction",
        repo_mapping = {
            "@com_google_absl": "@abseil-cpp",
            "@ocp": "@ocp-diag-core",
        },
    )
    external_http_archive(
        "proto-processing",
        location_name = "proto_processing",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:proto_processing_lib.patch"],
        repo_mapping = {
            "@com_google_absl": "@abseil-cpp",
            "@ocp": "@ocp-diag-core",
            "@com_google_protoconverter": "@proto-converter",
            "@com_google_protofieldextraction": "@proto-field-extraction",
        },
    )
    external_http_archive(
        name = "ocp-diag-core",
        location_name = "ocp",
        repo_mapping = {
            "@com_google_absl": "@abseil-cpp",
            "@com_google_googletest": "@googletest",
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

    _aws_c_auth_testdata()
    _liburing()
    _com_github_bazel_buildtools()
    _c_ares()
    _com_github_openhistogram_libcircllhist()
    _xxhash()
    _dd_trace_cpp()
    _tclap()
    _sql_parser()
    _fmt()
    _spdlog()
    _benchmark()
    _libprotobuf_mutator()
    _libsxg()
    _tcmalloc()
    _gperftools()
    _com_github_grpc_grpc()
    _rules_proto_grpc()
    _icu()
    _ipp_crypto()
    _numactl()
    _uadk()
    _qatlib()
    _qatzip()
    _qat_zstd()
    _lz4()
    _yaml_cpp()
    _libevent()
    _luajit()
    _nghttp2()
    _msgpack_cxx()
    _cpp2sky()
    _hessian2_codec()
    _nlohmann_json()
    _su_exec()
    _abseil_cpp()
    _googletest()
    _com_google_protobuf()
    _v8()
    _fast_float()
    _highway()
    _dragonbox()
    _fp16()
    _simdutf()
    _quiche()
    _googleurl()
    _hyperscan()
    _vectorscan()
    _io_opentelemetry_api_cpp()
    _colm()
    _ragel()
    _dlb()
    _zlib_ng()
    _boost()
    _brotli()
    _zstd()
    _re2()
    _proxy_wasm_cpp_sdk()
    _proxy_wasm_cpp_host()
    _emsdk()
    _rules_fuzzing()
    external_http_archive("proxy_wasm_rust_sdk")
    _cel_cpp()
    _perfetto()
    _rules_ruby()
    external_http_archive("flatbuffers")
    external_http_archive("bazel_features")
    external_http_archive("bazel_compdb")
    external_http_archive(
        name = "envoy_examples",
    )
    external_http_archive("envoy_toolshed")

    _libmaxminddb()
    _thrift()

    external_http_archive("rules_license")
    external_http_archive("rules_pkg")
    external_http_archive("shellcheck")

    external_http_archive(
        name = "yq.bzl",
        location_name = "yq_bzl",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:yq.patch"],
    )
    external_http_archive("aspect_bazel_lib")

    _vpp_vcl()

    _toolchains_llvm()

    _cc_deps()
    _go_deps(skip_targets)
    _rust_deps()
    _kafka_deps()
    _wamr()
    _wasmtime()

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
        name = "libcircllhist",
        build_file = "@envoy//bazel/external:libcircllhist.BUILD",
    )

def _aws_c_auth_testdata():
    external_http_archive(
        name = "aws-c-auth-testdata",
        location_name = "aws_c_auth_testdata",
        build_file = "@envoy//bazel/external:aws-c-auth.BUILD",
    )

def _liburing():
    external_http_archive(
        name = "liburing",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:liburing.patch"],
    )

def _com_github_bazel_buildtools():
    # TODO(phlax): Add binary download
    #  cf: https://github.com/bazelbuild/buildtools/issues/367
    external_http_archive(
        name = "buildtools",
    )

def _c_ares():
    external_http_archive(
        name = "c-ares",
        location_name = "c_ares",
        build_file = "@envoy//bazel/external:c-ares.BUILD",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:c-ares.patch"],
    )

def _xxhash():
    external_http_archive(
        name = "xxhash",
        build_file = "@envoy//bazel/external:xxhash.BUILD",
    )

def _sql_parser():
    external_http_archive(
        name = "sql-parser",
        location_name = "sql_parser",
        build_file = "@envoy//bazel/external:sqlparser.BUILD",
    )

def _tclap():
    external_http_archive(
        name = "tclap",
        build_file = "@envoy//bazel/external:tclap.BUILD",
        patch_args = ["-p1"],
    )

def _fmt():
    external_http_archive(
        name = "fmt",
        build_file = "@envoy//bazel/external:fmtlib.BUILD",
    )

def _spdlog():
    external_http_archive(
        name = "spdlog",
        build_file = "@envoy//bazel/external:spdlog.BUILD",
    )

def _benchmark():
    external_http_archive(
        name = "benchmark",
        repo_mapping = {"@com_google_googletest": "@googletest"},
    )
    external_http_archive(
        name = "libpfm",
        build_file = "@benchmark//tools:libpfm.BUILD.bazel",
    )

def _libprotobuf_mutator():
    external_http_archive(
        name = "libprotobuf-mutator",
        location_name = "libprotobuf_mutator",
        build_file = "@envoy//bazel/external:libprotobuf_mutator.BUILD",
    )

def _libsxg():
    external_http_archive(
        name = "libsxg",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _icu():
    external_http_archive(
        name = "icu",
        patches = ["@envoy//bazel/foreign_cc:icu.patch"],
        patch_args = ["-p1"],
        patch_cmds = [
            "sed -i 's/^#![[:space:]]*/#!/' source/configure source/config.sub source/config.guess source/mkinstalldirs",
            "sed -i 's/\\r$//' source/configure source/config.sub source/config.guess source/mkinstalldirs",
        ],
        build_file_content = BUILD_ALL_CONTENT,
    )

def _ipp_crypto():
    external_http_archive(
        name = "ipp-crypto",
        location_name = "ipp_crypto",
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

def _qatlib():
    external_http_archive(
        name = "qatlib",
        build_file_content = BUILD_ALL_CONTENT,
        patches = ["@envoy//bazel/foreign_cc:qatlib.patch"],
        patch_args = ["-p1"],
    )

def _qatzip():
    external_http_archive(
        name = "qatzip",
        build_file_content = BUILD_ALL_CONTENT,
        patches = ["@envoy//bazel/foreign_cc:qatzip.patch"],
        patch_args = ["-p1"],
    )

def _qat_zstd():
    external_http_archive(
        name = "qat-zstd",
        location_name = "qat_zstd",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:qatzstd.patch"],
    )

def _lz4():
    external_http_archive(
        name = "lz4",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _yaml_cpp():
    external_http_archive(
        name = "yaml-cpp",
        location_name = "yaml_cpp",
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
    )

def _libevent():
    LIBEVENT_BUILD_CONTENT = """%s\nalias(name = "libevent", actual = ":all", visibility = ["//visibility:public"])""" % BUILD_ALL_CONTENT
    external_http_archive(
        name = "libevent",
        build_file_content = LIBEVENT_BUILD_CONTENT,
    )

def _colm():
    external_http_archive(
        name = "colm",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _ragel():
    external_http_archive(
        name = "ragel",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _zlib_ng():
    external_http_archive(
        name = "zlib-ng",
        location_name = "zlib_ng",
        build_file = "@envoy//bazel/external:zlib_ng.BUILD",
    )

# Boost in general is not approved for Envoy use, and the header-only
# dependency is only for the Hyperscan contrib package.
def _boost():
    external_http_archive(
        name = "boost",
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

def _brotli():
    external_http_archive(
        name = "brotli",
    )

def _zstd():
    external_http_archive(
        name = "zstd",
        build_file = "@envoy//bazel/external:zstd.BUILD",
    )

def _cel_cpp():
    external_http_archive(
        name = "cel-cpp",
        location_name = "cel_cpp",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:cel-cpp.patch"],
        repo_mapping = {
            "@com_google_absl": "@abseil-cpp",
            "@com_google_cel_spec": "@cel-spec",
            "@com_github_google_flatbuffers": "@flatbuffers",
            "@com_googlesource_code_re2": "@re2",
        },
    )

    # Load required dependencies that cel-cpp expects.
    external_http_archive(
        "cel-spec",
        location_name = "cel_spec",
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
    )

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
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
    )

def _perfetto():
    external_http_archive(
        name = "perfetto",
        build_file_content = """
package(default_visibility = ["//visibility:public"])
cc_library(
    name = "perfetto",
    srcs = ["perfetto.cc"],
    hdrs = ["perfetto.h"],
)
""",
    )

def _nghttp2():
    external_http_archive(
        name = "nghttp2",
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

def _msgpack_cxx():
    external_http_archive(
        name = "msgpack-cxx",
        location_name = "msgpack_cxx",
        build_file = "@envoy//bazel/external:msgpack.BUILD",
    )

def _hyperscan():
    external_http_archive(
        name = "hyperscan",
        location_name = "hyperscan",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:hyperscan.patch"],
    )

def _vectorscan():
    external_http_archive(
        name = "vectorscan",
        location_name = "vectorscan",
        build_file_content = BUILD_ALL_CONTENT,
        type = "tar.gz",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:vectorscan.patch"],
    )

def _io_opentelemetry_api_cpp():
    external_http_archive(
        name = "opentelemetry-cpp",
        location_name = "opentelemetry_cpp",
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
    )

def _dd_trace_cpp():
    external_http_archive(
        "dd-trace-cpp",
        location_name = "dd_trace_cpp",
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
    )

def _cpp2sky():
    external_http_archive(
        name = "cpp2sky",
        patches = ["@envoy//bazel:com_github_skyapm_cpp2sky.patch"],
        patch_args = ["-p1"],
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
    )
    external_http_archive(
        name = "skywalking_data_collect_protocol",
    )

def _nlohmann_json():
    external_http_archive(
        name = "nlohmann_json",
    )

def _hessian2_codec():
    external_http_archive(
        name = "hessian2-codec",
        location_name = "hessian2_codec",
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
    )

def _su_exec():
    external_http_archive(
        name = "su-exec",
        location_name = "su_exec",
        build_file = "@envoy//bazel/external:su-exec.BUILD",
    )

def _googletest():
    external_http_archive(
        "googletest",
        patches = ["@envoy//bazel:googletest.patch"],
        patch_args = ["-p1"],
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
    )

# TODO(jmarantz): replace the use of bind and external_deps with just
# the direct Bazel path at all sites.  This will make it easier to
# pull in more bits of abseil as needed, and is now the preferred
# method for pure Bazel deps.
def _abseil_cpp():
    external_http_archive(
        name = "abseil-cpp",
        location_name = "abseil_cpp",
        patches = ["@envoy//bazel:abseil.patch"],
        patch_args = ["-p1"],
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
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
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
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
    )

def _v8():
    external_http_archive(
        name = "v8",
        patches = [
            "@envoy//bazel:v8.patch",
            "@envoy//bazel:v8_novtune.patch",
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

def _quiche():
    external_http_archive(
        name = "quiche",
        patch_cmds = ["find quiche/ -type f -name \"*.bazel\" -delete"],
        build_file = "@envoy//bazel/external:quiche.BUILD",
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
    )

def _googleurl():
    external_http_archive(
        name = "googleurl",
        patches = ["@envoy//bazel/external:googleurl.patch"],
        patch_args = ["-p1"],
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
    )

def _com_github_grpc_grpc():
    external_http_archive(
        name = "com_github_grpc_grpc",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:grpc.patch"],
        repo_mapping = {
            "@com_google_absl": "@abseil-cpp",
            "@com_github_cncf_xds": "@xds",
            "@com_googlesource_code_re2": "@re2",
            "@openssl": "@boringssl",
        },
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
    external_http_archive("re2")

def _proxy_wasm_cpp_sdk():
    external_http_archive(
        name = "proxy_wasm_cpp_sdk",
        patch_args = ["-p1"],
        patches = [
            "@envoy//bazel:proxy_wasm_cpp_sdk.patch",
        ],
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
    )

def _proxy_wasm_cpp_host():
    external_http_archive(
        name = "proxy_wasm_cpp_host",
        patch_args = ["-p1"],
        patches = [
            "@envoy//bazel:proxy_wasm_cpp_host.patch",
        ],
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
    )

def _emsdk():
    external_http_archive(
        name = "emsdk",
        patch_args = ["-p2"],
        patches = ["@envoy//bazel:emsdk.patch"],
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
    )

def _luajit():
    LUAJIT_BUILD_CONTENT = """%s\nalias(name = "luajit", actual = ":all", visibility = ["//visibility:public"])""" % BUILD_ALL_CONTENT
    external_http_archive(
        name = "luajit",
        build_file_content = LUAJIT_BUILD_CONTENT,
        patches = ["@envoy//bazel/foreign_cc:luajit.patch"],
        patch_args = ["-p1"],
    )

def _tcmalloc():
    external_http_archive(
        name = "tcmalloc",
        patches = ["@envoy//bazel:tcmalloc.patch"],
        patch_args = ["-p1"],
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
    )

def _gperftools():
    external_http_archive(
        name = "gperftools",
    )

def _wamr():
    external_http_archive(
        name = "wamr",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _toolchains_llvm():
    external_http_archive(name = "toolchains_llvm")

def _wasmtime():
    external_http_archive(
        name = "wasmtime",
        build_file = "@proxy_wasm_cpp_host//:bazel/external/wasmtime.BUILD",
        repo_mapping = {"@com_google_absl": "@abseil-cpp"},
    )

def _dlb():
    external_http_archive(
        name = "dlb",
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
            "@com_google_absl": "@abseil-cpp",
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

def _vpp_vcl():
    external_http_archive(
        name = "vpp-vcl",
        location_name = "vpp_vcl",
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

def _thrift():
    external_http_archive(
        name = "thrift",
        build_file = "@envoy//bazel/external:thrift.BUILD",
        patches = ["@envoy//bazel:thrift.patch"],
        patch_args = ["-p1"],
        patch_cmds = ["mv src thrift"],
    )

def _libmaxminddb():
    LIBMAXMINDDB_BUILD_CONTENT = """%s\nalias(name = "libmaxminddb", actual = ":all", visibility = ["//visibility:public"])""" % BUILD_ALL_CONTENT
    external_http_archive(
        name = "libmaxminddb",
        build_file_content = LIBMAXMINDDB_BUILD_CONTENT,
    )

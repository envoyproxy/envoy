load(":dev_binding.bzl", "envoy_dev_binding")
load(":genrule_repository.bzl", "genrule_repository")
load("@envoy_api//bazel:envoy_http_archive.bzl", "envoy_http_archive")
load("@envoy_api//bazel:external_deps.bzl", "load_repository_locations")
load(":repository_locations.bzl", "REPOSITORY_LOCATIONS_SPEC")
load("@com_google_googleapis//:repository_rules.bzl", "switched_rules_by_language")

PPC_SKIP_TARGETS = ["envoy.filters.http.lua"]

WINDOWS_SKIP_TARGETS = [
    "envoy.tracers.dynamic_ot",
    "envoy.tracers.lightstep",
    "envoy.tracers.datadog",
    "envoy.tracers.opencensus",
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

# Use this macro to reference any genrule_repository sourced from bazel/repository_locations.bzl.
def external_genrule_repository(name, **kwargs):
    location = REPOSITORY_LOCATIONS[name]
    genrule_repository(
        name = name,
        **dict(location, **kwargs)
    )

def _default_envoy_build_config_impl(ctx):
    ctx.file("WORKSPACE", "")
    ctx.file("BUILD.bazel", "")
    ctx.symlink(ctx.attr.config, "extensions_build_config.bzl")

_default_envoy_build_config = repository_rule(
    implementation = _default_envoy_build_config_impl,
    attrs = {
        "config": attr.label(default = "@envoy//source/extensions:extensions_build_config.bzl"),
    },
)

# Python dependencies.
def _python_deps():
    # TODO(htuch): convert these to pip3_import.
    external_http_archive(
        name = "com_github_twitter_common_lang",
        build_file = "@envoy//bazel/external:twitter_common_lang.BUILD",
    )
    external_http_archive(
        name = "com_github_twitter_common_rpc",
        build_file = "@envoy//bazel/external:twitter_common_rpc.BUILD",
    )
    external_http_archive(
        name = "com_github_twitter_common_finagle_thrift",
        build_file = "@envoy//bazel/external:twitter_common_finagle_thrift.BUILD",
    )
    external_http_archive(
        name = "six",
        build_file = "@com_google_protobuf//third_party:six.BUILD",
    )

# Bazel native C++ dependencies. For the dependencies that doesn't provide autoconf/automake builds.
def _cc_deps():
    external_http_archive("grpc_httpjson_transcoding")
    native.bind(
        name = "path_matcher",
        actual = "@grpc_httpjson_transcoding//src:path_matcher",
    )
    native.bind(
        name = "grpc_transcoding",
        actual = "@grpc_httpjson_transcoding//src:transcoding",
    )

def _go_deps(skip_targets):
    # Keep the skip_targets check around until Istio Proxy has stopped using
    # it to exclude the Go rules.
    if "io_bazel_rules_go" not in skip_targets:
        external_http_archive(
            name = "io_bazel_rules_go",
            # TODO(wrowe, sunjayBhatia): remove when Windows RBE supports batch file invocation
            patch_args = ["-p1"],
            patches = ["@envoy//bazel:rules_go.patch"],
        )
        external_http_archive("bazel_gazelle")

def _rust_deps():
    external_http_archive("io_bazel_rules_rust")

def envoy_dependencies(skip_targets = []):
    # Setup Envoy developer tools.
    envoy_dev_binding()

    # Treat Envoy's overall build config as an external repo, so projects that
    # build Envoy as a subcomponent can easily override the config.
    if "envoy_build_config" not in native.existing_rules().keys():
        _default_envoy_build_config(name = "envoy_build_config")

    # Setup external Bazel rules
    _foreign_cc_dependencies()

    # Binding to an alias pointing to the selected version of BoringSSL:
    # - BoringSSL FIPS from @boringssl_fips//:ssl,
    # - non-FIPS BoringSSL from @boringssl//:ssl.
    _boringssl()
    _boringssl_fips()
    native.bind(
        name = "ssl",
        actual = "@envoy//bazel:boringssl",
    )

    # The long repo names (`com_github_fmtlib_fmt` instead of `fmtlib`) are
    # semi-standard in the Bazel community, intended to avoid both duplicate
    # dependencies and name conflicts.
    _com_github_c_ares_c_ares()
    _com_github_circonus_labs_libcircllhist()
    _com_github_cyan4973_xxhash()
    _com_github_datadog_dd_opentracing_cpp()
    _com_github_mirror_tclap()
    _com_github_envoyproxy_sqlparser()
    _com_github_fmtlib_fmt()
    _com_github_gabime_spdlog()
    _com_github_google_benchmark()
    _com_github_google_jwt_verify()
    _com_github_google_libprotobuf_mutator()
    _com_github_google_tcmalloc()
    _com_github_gperftools_gperftools()
    _com_github_grpc_grpc()
    _com_github_jbeder_yaml_cpp()
    _com_github_libevent_libevent()
    _com_github_luajit_luajit()
    _com_github_moonjit_moonjit()
    _com_github_nghttp2_nghttp2()
    _com_github_skyapm_cpp2sky()
    _com_github_nodejs_http_parser()
    _com_github_tencent_rapidjson()
    _com_google_absl()
    _com_google_googletest()
    _com_google_protobuf()
    _io_opencensus_cpp()
    _com_github_curl()
    _com_github_envoyproxy_sqlparser()
    _com_googlesource_chromium_v8()
    _com_googlesource_quiche()
    _com_googlesource_googleurl()
    _com_lightstep_tracer_cpp()
    _io_opentracing_cpp()
    _net_zlib()
    _com_github_zlib_ng_zlib_ng()
    _upb()
    _proxy_wasm_cpp_sdk()
    _proxy_wasm_cpp_host()
    _emscripten_toolchain()
    external_http_archive("proxy_wasm_rust_sdk")
    external_http_archive("com_googlesource_code_re2")
    _com_google_cel_cpp()
    external_http_archive("com_github_google_flatbuffers")
    external_http_archive("bazel_toolchains")
    external_http_archive("bazel_compdb")
    external_http_archive("envoy_build_tools")
    external_http_archive("rules_cc")

    # Unconditional, since we use this only for compiler-agnostic fuzzing utils.
    _org_llvm_releases_compiler_rt()

    _python_deps()
    _cc_deps()
    _go_deps(skip_targets)
    _rust_deps()
    _kafka_deps()

    _org_llvm_llvm()
    _com_github_wavm_wavm()
    _com_github_wasmtime()
    _com_github_wasm_c_api()

    switched_rules_by_language(
        name = "com_google_googleapis_imports",
        cc = True,
        go = True,
        grpc = True,
        rules_override = {
            "py_proto_library": "@envoy_api//bazel:api_build_system.bzl",
        },
    )
    native.bind(
        name = "bazel_runfiles",
        actual = "@bazel_tools//tools/cpp/runfiles",
    )

def _boringssl():
    external_http_archive(
        name = "boringssl",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:boringssl_static.patch"],
    )

def _boringssl_fips():
    external_genrule_repository(
        name = "boringssl_fips",
        genrule_cmd_file = "@envoy//bazel/external:boringssl_fips.genrule_cmd",
        build_file = "@envoy//bazel/external:boringssl_fips.BUILD",
        patches = ["@envoy//bazel/external:boringssl_fips.patch"],
    )

def _com_github_circonus_labs_libcircllhist():
    external_http_archive(
        name = "com_github_circonus_labs_libcircllhist",
        build_file = "@envoy//bazel/external:libcircllhist.BUILD",
    )
    native.bind(
        name = "libcircllhist",
        actual = "@com_github_circonus_labs_libcircllhist//:libcircllhist",
    )

def _com_github_c_ares_c_ares():
    external_http_archive(
        name = "com_github_c_ares_c_ares",
        build_file_content = BUILD_ALL_CONTENT,
    )
    native.bind(
        name = "ares",
        actual = "@envoy//bazel/foreign_cc:ares",
    )

def _com_github_cyan4973_xxhash():
    external_http_archive(
        name = "com_github_cyan4973_xxhash",
        build_file = "@envoy//bazel/external:xxhash.BUILD",
    )
    native.bind(
        name = "xxhash",
        actual = "@com_github_cyan4973_xxhash//:xxhash",
    )

def _com_github_envoyproxy_sqlparser():
    external_http_archive(
        name = "com_github_envoyproxy_sqlparser",
        build_file = "@envoy//bazel/external:sqlparser.BUILD",
    )
    native.bind(
        name = "sqlparser",
        actual = "@com_github_envoyproxy_sqlparser//:sqlparser",
    )

def _com_github_mirror_tclap():
    external_http_archive(
        name = "com_github_mirror_tclap",
        build_file = "@envoy//bazel/external:tclap.BUILD",
        patch_args = ["-p1"],
        # If and when we pick up tclap 1.4 or later release,
        # this entire issue was refactored away 6 years ago;
        # https://sourceforge.net/p/tclap/code/ci/5d4ffbf2db794af799b8c5727fb6c65c079195ac/
        # https://github.com/envoyproxy/envoy/pull/8572#discussion_r337554195
        patches = ["@envoy//bazel:tclap-win64-ull-sizet.patch"],
    )
    native.bind(
        name = "tclap",
        actual = "@com_github_mirror_tclap//:tclap",
    )

def _com_github_fmtlib_fmt():
    external_http_archive(
        name = "com_github_fmtlib_fmt",
        build_file = "@envoy//bazel/external:fmtlib.BUILD",
    )
    native.bind(
        name = "fmtlib",
        actual = "@com_github_fmtlib_fmt//:fmtlib",
    )

def _com_github_gabime_spdlog():
    external_http_archive(
        name = "com_github_gabime_spdlog",
        build_file = "@envoy//bazel/external:spdlog.BUILD",
    )
    native.bind(
        name = "spdlog",
        actual = "@com_github_gabime_spdlog//:spdlog",
    )

def _com_github_google_benchmark():
    external_http_archive(
        name = "com_github_google_benchmark",
    )
    native.bind(
        name = "benchmark",
        actual = "@com_github_google_benchmark//:benchmark",
    )

def _com_github_google_libprotobuf_mutator():
    external_http_archive(
        name = "com_github_google_libprotobuf_mutator",
        build_file = "@envoy//bazel/external:libprotobuf_mutator.BUILD",
    )

def _com_github_jbeder_yaml_cpp():
    external_http_archive(
        name = "com_github_jbeder_yaml_cpp",
    )
    native.bind(
        name = "yaml_cpp",
        actual = "@com_github_jbeder_yaml_cpp//:yaml-cpp",
    )

def _com_github_libevent_libevent():
    external_http_archive(
        name = "com_github_libevent_libevent",
        build_file_content = BUILD_ALL_CONTENT,
    )
    native.bind(
        name = "event",
        actual = "@envoy//bazel/foreign_cc:event",
    )

def _net_zlib():
    external_http_archive(
        name = "net_zlib",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:zlib.patch"],
    )

    native.bind(
        name = "zlib",
        actual = "@envoy//bazel/foreign_cc:zlib",
    )

    # Bind for grpc.
    native.bind(
        name = "madler_zlib",
        actual = "@envoy//bazel/foreign_cc:zlib",
    )

def _com_github_zlib_ng_zlib_ng():
    external_http_archive(
        name = "com_github_zlib_ng_zlib_ng",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:zlib_ng.patch"],
    )

def _com_google_cel_cpp():
    external_http_archive("com_google_cel_cpp")
    external_http_archive("rules_antlr")

    # Parser dependencies
    # TODO: upgrade this when cel is upgraded to use the latest version
    external_http_archive(name = "rules_antlr")
    external_http_archive(
        name = "antlr4_runtimes",
        build_file_content = """
package(default_visibility = ["//visibility:public"])
cc_library(
    name = "cpp",
    srcs = glob(["runtime/Cpp/runtime/src/**/*.cpp"]),
    hdrs = glob(["runtime/Cpp/runtime/src/**/*.h"]),
    includes = ["runtime/Cpp/runtime/src"],
)
""",
        patch_args = ["-p1"],
        # Patches ASAN violation of initialization fiasco
        patches = ["@envoy//bazel:antlr.patch"],
    )

def _com_github_nghttp2_nghttp2():
    external_http_archive(
        name = "com_github_nghttp2_nghttp2",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p1"],
        # This patch cannot be picked up due to ABI rules. Discussion at;
        # https://github.com/nghttp2/nghttp2/pull/1395
        # https://github.com/envoyproxy/envoy/pull/8572#discussion_r334067786
        patches = ["@envoy//bazel/foreign_cc:nghttp2.patch"],
    )
    native.bind(
        name = "nghttp2",
        actual = "@envoy//bazel/foreign_cc:nghttp2",
    )

def _io_opentracing_cpp():
    external_http_archive(
        name = "io_opentracing_cpp",
        patch_args = ["-p1"],
        # Workaround for LSAN false positive in https://github.com/envoyproxy/envoy/issues/7647
        patches = ["@envoy//bazel:io_opentracing_cpp.patch"],
    )
    native.bind(
        name = "opentracing",
        actual = "@io_opentracing_cpp//:opentracing",
    )

def _com_lightstep_tracer_cpp():
    external_http_archive("com_lightstep_tracer_cpp")
    native.bind(
        name = "lightstep",
        actual = "@com_lightstep_tracer_cpp//:manual_tracer_lib",
    )

def _com_github_datadog_dd_opentracing_cpp():
    external_http_archive("com_github_datadog_dd_opentracing_cpp")
    external_http_archive(
        name = "com_github_msgpack_msgpack_c",
        build_file = "@com_github_datadog_dd_opentracing_cpp//:bazel/external/msgpack.BUILD",
    )
    native.bind(
        name = "dd_opentracing_cpp",
        actual = "@com_github_datadog_dd_opentracing_cpp//:dd_opentracing_cpp",
    )

def _com_github_skyapm_cpp2sky():
    external_http_archive(
        name = "com_github_skyapm_cpp2sky",
    )
    external_http_archive(
        name = "skywalking_data_collect_protocol",
    )
    native.bind(
        name = "cpp2sky",
        actual = "@com_github_skyapm_cpp2sky//source:cpp2sky_data_lib",
    )

def _com_github_tencent_rapidjson():
    external_http_archive(
        name = "com_github_tencent_rapidjson",
        build_file = "@envoy//bazel/external:rapidjson.BUILD",
    )
    native.bind(
        name = "rapidjson",
        actual = "@com_github_tencent_rapidjson//:rapidjson",
    )

def _com_github_nodejs_http_parser():
    external_http_archive(
        name = "com_github_nodejs_http_parser",
        build_file = "@envoy//bazel/external:http-parser.BUILD",
    )
    native.bind(
        name = "http_parser",
        actual = "@com_github_nodejs_http_parser//:http_parser",
    )

def _com_google_googletest():
    external_http_archive("com_google_googletest")
    native.bind(
        name = "googletest",
        actual = "@com_google_googletest//:gtest",
    )

# TODO(jmarantz): replace the use of bind and external_deps with just
# the direct Bazel path at all sites.  This will make it easier to
# pull in more bits of abseil as needed, and is now the preferred
# method for pure Bazel deps.
def _com_google_absl():
    external_http_archive("com_google_absl")
    native.bind(
        name = "abseil_any",
        actual = "@com_google_absl//absl/types:any",
    )
    native.bind(
        name = "abseil_base",
        actual = "@com_google_absl//absl/base:base",
    )

    # Bind for grpc.
    native.bind(
        name = "absl-base",
        actual = "@com_google_absl//absl/base",
    )
    native.bind(
        name = "abseil_flat_hash_map",
        actual = "@com_google_absl//absl/container:flat_hash_map",
    )
    native.bind(
        name = "abseil_flat_hash_set",
        actual = "@com_google_absl//absl/container:flat_hash_set",
    )
    native.bind(
        name = "abseil_hash",
        actual = "@com_google_absl//absl/hash:hash",
    )
    native.bind(
        name = "abseil_hash_testing",
        actual = "@com_google_absl//absl/hash:hash_testing",
    )
    native.bind(
        name = "abseil_inlined_vector",
        actual = "@com_google_absl//absl/container:inlined_vector",
    )
    native.bind(
        name = "abseil_memory",
        actual = "@com_google_absl//absl/memory:memory",
    )
    native.bind(
        name = "abseil_node_hash_map",
        actual = "@com_google_absl//absl/container:node_hash_map",
    )
    native.bind(
        name = "abseil_node_hash_set",
        actual = "@com_google_absl//absl/container:node_hash_set",
    )
    native.bind(
        name = "abseil_str_format",
        actual = "@com_google_absl//absl/strings:str_format",
    )
    native.bind(
        name = "abseil_strings",
        actual = "@com_google_absl//absl/strings:strings",
    )
    native.bind(
        name = "abseil_int128",
        actual = "@com_google_absl//absl/numeric:int128",
    )
    native.bind(
        name = "abseil_optional",
        actual = "@com_google_absl//absl/types:optional",
    )
    native.bind(
        name = "abseil_synchronization",
        actual = "@com_google_absl//absl/synchronization:synchronization",
    )
    native.bind(
        name = "abseil_symbolize",
        actual = "@com_google_absl//absl/debugging:symbolize",
    )
    native.bind(
        name = "abseil_stacktrace",
        actual = "@com_google_absl//absl/debugging:stacktrace",
    )

    # Require abseil_time as an indirect dependency as it is needed by the
    # direct dependency jwt_verify_lib.
    native.bind(
        name = "abseil_time",
        actual = "@com_google_absl//absl/time:time",
    )

    # Bind for grpc.
    native.bind(
        name = "absl-time",
        actual = "@com_google_absl//absl/time:time",
    )

    native.bind(
        name = "abseil_algorithm",
        actual = "@com_google_absl//absl/algorithm:algorithm",
    )
    native.bind(
        name = "abseil_variant",
        actual = "@com_google_absl//absl/types:variant",
    )
    native.bind(
        name = "abseil_status",
        actual = "@com_google_absl//absl/status",
    )

def _com_google_protobuf():
    external_http_archive("rules_python")
    external_http_archive(
        "com_google_protobuf",
        patches = ["@envoy//bazel:protobuf.patch"],
        patch_args = ["-p1"],
    )

    native.bind(
        name = "protobuf",
        actual = "@com_google_protobuf//:protobuf",
    )
    native.bind(
        name = "protobuf_clib",
        actual = "@com_google_protobuf//:protoc_lib",
    )
    native.bind(
        name = "protocol_compiler",
        actual = "@com_google_protobuf//:protoc",
    )
    native.bind(
        name = "protoc",
        actual = "@com_google_protobuf//:protoc",
    )

    # Needed for `bazel fetch` to work with @com_google_protobuf
    # https://github.com/google/protobuf/blob/v3.6.1/util/python/BUILD#L6-L9
    native.bind(
        name = "python_headers",
        actual = "@com_google_protobuf//util/python:python_headers",
    )

def _io_opencensus_cpp():
    external_http_archive(
        name = "io_opencensus_cpp",
    )
    native.bind(
        name = "opencensus_trace",
        actual = "@io_opencensus_cpp//opencensus/trace",
    )
    native.bind(
        name = "opencensus_trace_b3",
        actual = "@io_opencensus_cpp//opencensus/trace:b3",
    )
    native.bind(
        name = "opencensus_trace_cloud_trace_context",
        actual = "@io_opencensus_cpp//opencensus/trace:cloud_trace_context",
    )
    native.bind(
        name = "opencensus_trace_grpc_trace_bin",
        actual = "@io_opencensus_cpp//opencensus/trace:grpc_trace_bin",
    )
    native.bind(
        name = "opencensus_trace_trace_context",
        actual = "@io_opencensus_cpp//opencensus/trace:trace_context",
    )
    native.bind(
        name = "opencensus_exporter_ocagent",
        actual = "@io_opencensus_cpp//opencensus/exporters/trace/ocagent:ocagent_exporter",
    )
    native.bind(
        name = "opencensus_exporter_stdout",
        actual = "@io_opencensus_cpp//opencensus/exporters/trace/stdout:stdout_exporter",
    )
    native.bind(
        name = "opencensus_exporter_stackdriver",
        actual = "@io_opencensus_cpp//opencensus/exporters/trace/stackdriver:stackdriver_exporter",
    )
    native.bind(
        name = "opencensus_exporter_zipkin",
        actual = "@io_opencensus_cpp//opencensus/exporters/trace/zipkin:zipkin_exporter",
    )

def _com_github_curl():
    # Used by OpenCensus Zipkin exporter.
    external_http_archive(
        name = "com_github_curl",
        build_file_content = BUILD_ALL_CONTENT + """
cc_library(name = "curl", visibility = ["//visibility:public"], deps = ["@envoy//bazel/foreign_cc:curl"])
""",
        # Patch curl 7.74.0 due to CMake's problematic implementation of policy `CMP0091`
        # and introduction of libidn2 dependency which is inconsistently available and must
        # not be a dynamic dependency on linux.
        # Upstream patches submitted: https://github.com/curl/curl/pull/6050 & 6362
        # TODO(https://github.com/envoyproxy/envoy/issues/11816): This patch is obsoleted
        # by elimination of the curl dependency.
        patches = ["@envoy//bazel/foreign_cc:curl.patch"],
        patch_args = ["-p1"],
    )
    native.bind(
        name = "curl",
        actual = "@envoy//bazel/foreign_cc:curl",
    )

def _com_googlesource_chromium_v8():
    external_genrule_repository(
        name = "com_googlesource_chromium_v8",
        genrule_cmd_file = "@envoy//bazel/external:wee8.genrule_cmd",
        build_file = "@envoy//bazel/external:wee8.BUILD",
        patches = ["@envoy//bazel/external:wee8.patch"],
    )
    native.bind(
        name = "wee8",
        actual = "@com_googlesource_chromium_v8//:wee8",
    )

def _com_googlesource_quiche():
    external_genrule_repository(
        name = "com_googlesource_quiche",
        genrule_cmd_file = "@envoy//bazel/external:quiche.genrule_cmd",
        build_file = "@envoy//bazel/external:quiche.BUILD",
    )
    native.bind(
        name = "quiche_common_platform",
        actual = "@com_googlesource_quiche//:quiche_common_platform",
    )
    native.bind(
        name = "quiche_http2_platform",
        actual = "@com_googlesource_quiche//:http2_platform",
    )
    native.bind(
        name = "quiche_spdy_platform",
        actual = "@com_googlesource_quiche//:spdy_platform",
    )
    native.bind(
        name = "quiche_quic_platform",
        actual = "@com_googlesource_quiche//:quic_platform",
    )
    native.bind(
        name = "quiche_quic_platform_base",
        actual = "@com_googlesource_quiche//:quic_platform_base",
    )

def _com_googlesource_googleurl():
    external_http_archive(
        name = "com_googlesource_googleurl",
        patches = ["@envoy//bazel/external:googleurl.patch"],
        patch_args = ["-p1"],
    )

def _org_llvm_releases_compiler_rt():
    external_http_archive(
        name = "org_llvm_releases_compiler_rt",
        build_file = "@envoy//bazel/external:compiler_rt.BUILD",
    )

def _com_github_grpc_grpc():
    external_http_archive("com_github_grpc_grpc")
    external_http_archive("build_bazel_rules_apple")

    # Rebind some stuff to match what the gRPC Bazel is expecting.
    native.bind(
        name = "protobuf_headers",
        actual = "@com_google_protobuf//:protobuf_headers",
    )
    native.bind(
        name = "libssl",
        actual = "//external:ssl",
    )
    native.bind(
        name = "cares",
        actual = "//external:ares",
    )

    native.bind(
        name = "grpc",
        actual = "@com_github_grpc_grpc//:grpc++",
    )

    native.bind(
        name = "grpc_health_proto",
        actual = "@envoy//bazel:grpc_health_proto",
    )

    native.bind(
        name = "grpc_alts_fake_handshaker_server",
        actual = "@com_github_grpc_grpc//test/core/tsi/alts/fake_handshaker:fake_handshaker_lib",
    )

    native.bind(
        name = "grpc_alts_handshaker_proto",
        actual = "@com_github_grpc_grpc//test/core/tsi/alts/fake_handshaker:handshaker_proto",
    )

    native.bind(
        name = "grpc_alts_transport_security_common_proto",
        actual = "@com_github_grpc_grpc//test/core/tsi/alts/fake_handshaker:transport_security_common_proto",
    )

    native.bind(
        name = "re2",
        actual = "@com_googlesource_code_re2//:re2",
    )

    native.bind(
        name = "upb_lib_descriptor",
        actual = "@upb//:descriptor_upb_proto",
    )

    native.bind(
        name = "upb_textformat_lib",
        actual = "@upb//:textformat",
    )

def _upb():
    external_http_archive(name = "upb")

    native.bind(
        name = "upb_lib",
        actual = "@upb//:upb",
    )

def _proxy_wasm_cpp_sdk():
    external_http_archive(name = "proxy_wasm_cpp_sdk")

def _proxy_wasm_cpp_host():
    external_http_archive(
        name = "proxy_wasm_cpp_host",
        build_file = "@envoy//bazel/external:proxy_wasm_cpp_host.BUILD",
    )

def _emscripten_toolchain():
    external_http_archive(
        name = "emscripten_toolchain",
        build_file_content = _build_all_content(exclude = [
            "upstream/emscripten/cache/is_vanilla.txt",
            ".emscripten_sanity",
        ]),
        patch_cmds = [
            "if [[ \"$(uname -m)\" == \"x86_64\" ]]; then ./emsdk install 2.0.7 && ./emsdk activate --embedded 2.0.7; fi",
        ],
    )

def _com_github_google_jwt_verify():
    external_http_archive("com_github_google_jwt_verify")

    native.bind(
        name = "jwt_verify_lib",
        actual = "@com_github_google_jwt_verify//:jwt_verify_lib",
    )

def _com_github_luajit_luajit():
    external_http_archive(
        name = "com_github_luajit_luajit",
        build_file_content = BUILD_ALL_CONTENT,
        patches = ["@envoy//bazel/foreign_cc:luajit.patch"],
        patch_args = ["-p1"],
        patch_cmds = ["chmod u+x build.py"],
    )

    native.bind(
        name = "luajit",
        actual = "@envoy//bazel/foreign_cc:luajit",
    )

def _com_github_moonjit_moonjit():
    external_http_archive(
        name = "com_github_moonjit_moonjit",
        build_file_content = BUILD_ALL_CONTENT,
        patches = ["@envoy//bazel/foreign_cc:moonjit.patch"],
        patch_args = ["-p1"],
        patch_cmds = ["chmod u+x build.py"],
    )

    native.bind(
        name = "moonjit",
        actual = "@envoy//bazel/foreign_cc:moonjit",
    )

def _com_github_google_tcmalloc():
    external_http_archive(
        name = "com_github_google_tcmalloc",
    )

    native.bind(
        name = "tcmalloc",
        actual = "@com_github_google_tcmalloc//tcmalloc",
    )

def _com_github_gperftools_gperftools():
    external_http_archive(
        name = "com_github_gperftools_gperftools",
        build_file_content = BUILD_ALL_CONTENT,
    )
    native.bind(
        name = "gperftools",
        actual = "@envoy//bazel/foreign_cc:gperftools",
    )

def _org_llvm_llvm():
    external_http_archive(
        name = "org_llvm_llvm",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:llvm.patch"],
    )
    native.bind(
        name = "llvm",
        actual = "@envoy//bazel/foreign_cc:llvm",
    )

def _com_github_wavm_wavm():
    external_http_archive(
        name = "com_github_wavm_wavm",
        build_file_content = BUILD_ALL_CONTENT,
    )
    native.bind(
        name = "wavm",
        actual = "@envoy//bazel/foreign_cc:wavm",
    )

def _com_github_wasmtime():
    external_http_archive(
        name = "com_github_wasmtime",
        build_file = "@envoy//bazel/external:wasmtime.BUILD",
    )

def _com_github_wasm_c_api():
    external_http_archive(
        name = "com_github_wasm_c_api",
        build_file = "@envoy//bazel/external:wasm-c-api.BUILD",
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
        patches = ["@envoy//bazel/external:kafka_int32.patch"],
    )

    # This archive provides Kafka (and Zookeeper) binaries, that are used during Kafka integration
    # tests.
    external_http_archive(
        name = "kafka_server_binary",
        build_file_content = BUILD_ALL_CONTENT,
    )

    # This archive provides Kafka client in Python, so we can use it to interact with Kafka server
    # during interation tests.
    external_http_archive(
        name = "kafka_python_client",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _foreign_cc_dependencies():
    external_http_archive("rules_foreign_cc")

def _is_linux(ctxt):
    return ctxt.os.name == "linux"

def _is_arch(ctxt, arch):
    res = ctxt.execute(["uname", "-m"])
    return arch in res.stdout

def _is_linux_ppc(ctxt):
    return _is_linux(ctxt) and _is_arch(ctxt, "ppc")

def _is_linux_s390x(ctxt):
    return _is_linux(ctxt) and _is_arch(ctxt, "s390x")

def _is_linux_x86_64(ctxt):
    return _is_linux(ctxt) and _is_arch(ctxt, "x86_64")

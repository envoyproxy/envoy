load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load(":genrule_repository.bzl", "genrule_repository")
load("@envoy_api//bazel:envoy_http_archive.bzl", "envoy_http_archive")
load(":repository_locations.bzl", "REPOSITORY_LOCATIONS")
load("@com_google_googleapis//:repository_rules.bzl", "switched_rules_by_language")

# dict of {build recipe name: longform extension name,}
PPC_SKIP_TARGETS = {"luajit": "envoy.filters.http.lua"}

# Make all contents of an external repository accessible under a filegroup.  Used for external HTTP
# archives, e.g. cares.
BUILD_ALL_CONTENT = """filegroup(name = "all", srcs = glob(["**"]), visibility = ["//visibility:public"])"""

def _repository_impl(name, **kwargs):
    envoy_http_archive(
        name,
        locations = REPOSITORY_LOCATIONS,
        **kwargs
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

# Python dependencies. If these become non-trivial, we might be better off using a virtualenv to
# wrap them, but for now we can treat them as first-class Bazel.
def _python_deps():
    _repository_impl(
        name = "com_github_pallets_markupsafe",
        build_file = "@envoy//bazel/external:markupsafe.BUILD",
    )
    native.bind(
        name = "markupsafe",
        actual = "@com_github_pallets_markupsafe//:markupsafe",
    )
    _repository_impl(
        name = "com_github_pallets_jinja",
        build_file = "@envoy//bazel/external:jinja.BUILD",
    )
    native.bind(
        name = "jinja2",
        actual = "@com_github_pallets_jinja//:jinja2",
    )
    _repository_impl(
        name = "com_github_apache_thrift",
        build_file = "@envoy//bazel/external:apache_thrift.BUILD",
    )
    _repository_impl(
        name = "com_github_twitter_common_lang",
        build_file = "@envoy//bazel/external:twitter_common_lang.BUILD",
    )
    _repository_impl(
        name = "com_github_twitter_common_rpc",
        build_file = "@envoy//bazel/external:twitter_common_rpc.BUILD",
    )
    _repository_impl(
        name = "com_github_twitter_common_finagle_thrift",
        build_file = "@envoy//bazel/external:twitter_common_finagle_thrift.BUILD",
    )
    _repository_impl(
        name = "six_archive",
        build_file = "@com_google_protobuf//:six.BUILD",
    )
    native.bind(
        name = "six",
        actual = "@six_archive//:six",
    )

# Bazel native C++ dependencies. For the dependencies that doesn't provide autoconf/automake builds.
def _cc_deps():
    _repository_impl("grpc_httpjson_transcoding")
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
        _repository_impl("io_bazel_rules_go")
        _repository_impl("bazel_gazelle")

def envoy_dependencies(skip_targets = []):
    # Treat Envoy's overall build config as an external repo, so projects that
    # build Envoy as a subcomponent can easily override the config.
    if "envoy_build_config" not in native.existing_rules().keys():
        _default_envoy_build_config(name = "envoy_build_config")

    # Setup external Bazel rules
    _foreign_cc_dependencies()
    _rules_proto_dependencies()

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
    _com_github_eile_tclap()
    _com_github_envoyproxy_sqlparser()
    _com_github_fmtlib_fmt()
    _com_github_gabime_spdlog()
    _com_github_google_benchmark()
    _com_github_google_jwt_verify()
    _com_github_google_libprotobuf_mutator()
    _com_github_gperftools_gperftools()
    _com_github_grpc_grpc()
    _com_github_jbeder_yaml_cpp()
    _com_github_libevent_libevent()
    _com_github_luajit_luajit()
    _com_github_nanopb_nanopb()
    _com_github_nghttp2_nghttp2()
    _com_github_nodejs_http_parser()
    _com_github_tencent_rapidjson()
    _com_google_absl()
    _com_google_googletest()
    _com_google_protobuf()
    _io_opencensus_cpp()
    _com_github_curl()
    _com_github_envoyproxy_sqlparser()
    _com_googlesource_quiche()
    _com_lightstep_tracer_cpp()
    _io_opentracing_cpp()
    _net_zlib()
    _repository_impl("com_googlesource_code_re2")
    _com_google_cel_cpp()
    _repository_impl("bazel_toolchains")
    _repository_impl("bazel_compdb")
    _repository_impl("envoy_build_tools")

    # Unconditional, since we use this only for compiler-agnostic fuzzing utils.
    _org_llvm_releases_compiler_rt()
    _fuzzit_linux()

    _python_deps()
    _cc_deps()
    _go_deps(skip_targets)

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
    _repository_impl(
        name = "boringssl",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:boringssl_static.patch"],
    )

def _boringssl_fips():
    location = REPOSITORY_LOCATIONS["boringssl_fips"]
    genrule_repository(
        name = "boringssl_fips",
        urls = location["urls"],
        sha256 = location["sha256"],
        genrule_cmd_file = "@envoy//bazel/external:boringssl_fips.genrule_cmd",
        build_file = "@envoy//bazel/external:boringssl_fips.BUILD",
    )

def _com_github_circonus_labs_libcircllhist():
    _repository_impl(
        name = "com_github_circonus_labs_libcircllhist",
        build_file = "@envoy//bazel/external:libcircllhist.BUILD",
    )
    native.bind(
        name = "libcircllhist",
        actual = "@com_github_circonus_labs_libcircllhist//:libcircllhist",
    )

def _com_github_c_ares_c_ares():
    location = REPOSITORY_LOCATIONS["com_github_c_ares_c_ares"]
    http_archive(
        name = "com_github_c_ares_c_ares",
        build_file_content = BUILD_ALL_CONTENT,
        **location
    )
    native.bind(
        name = "ares",
        actual = "@envoy//bazel/foreign_cc:ares",
    )

def _com_github_cyan4973_xxhash():
    _repository_impl(
        name = "com_github_cyan4973_xxhash",
        build_file = "@envoy//bazel/external:xxhash.BUILD",
    )
    native.bind(
        name = "xxhash",
        actual = "@com_github_cyan4973_xxhash//:xxhash",
    )

def _com_github_envoyproxy_sqlparser():
    _repository_impl(
        name = "com_github_envoyproxy_sqlparser",
        build_file = "@envoy//bazel/external:sqlparser.BUILD",
    )
    native.bind(
        name = "sqlparser",
        actual = "@com_github_envoyproxy_sqlparser//:sqlparser",
    )

def _com_github_eile_tclap():
    _repository_impl(
        name = "com_github_eile_tclap",
        build_file = "@envoy//bazel/external:tclap.BUILD",
    )
    native.bind(
        name = "tclap",
        actual = "@com_github_eile_tclap//:tclap",
    )

def _com_github_fmtlib_fmt():
    _repository_impl(
        name = "com_github_fmtlib_fmt",
        build_file = "@envoy//bazel/external:fmtlib.BUILD",
    )
    native.bind(
        name = "fmtlib",
        actual = "@com_github_fmtlib_fmt//:fmtlib",
    )

def _com_github_gabime_spdlog():
    _repository_impl(
        name = "com_github_gabime_spdlog",
        build_file = "@envoy//bazel/external:spdlog.BUILD",
    )
    native.bind(
        name = "spdlog",
        actual = "@com_github_gabime_spdlog//:spdlog",
    )

def _com_github_google_benchmark():
    location = REPOSITORY_LOCATIONS["com_github_google_benchmark"]
    http_archive(
        name = "com_github_google_benchmark",
        **location
    )
    native.bind(
        name = "benchmark",
        actual = "@com_github_google_benchmark//:benchmark",
    )

def _com_github_google_libprotobuf_mutator():
    _repository_impl(
        name = "com_github_google_libprotobuf_mutator",
        build_file = "@envoy//bazel/external:libprotobuf_mutator.BUILD",
    )

def _com_github_jbeder_yaml_cpp():
    location = REPOSITORY_LOCATIONS["com_github_jbeder_yaml_cpp"]
    http_archive(
        name = "com_github_jbeder_yaml_cpp",
        build_file_content = BUILD_ALL_CONTENT,
        **location
    )
    native.bind(
        name = "yaml_cpp",
        actual = "@envoy//bazel/foreign_cc:yaml",
    )

def _com_github_libevent_libevent():
    location = REPOSITORY_LOCATIONS["com_github_libevent_libevent"]
    http_archive(
        name = "com_github_libevent_libevent",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p0"],
        patches = ["@envoy//bazel/foreign_cc:libevent_msvc.patch"],
        **location
    )
    native.bind(
        name = "event",
        actual = "@envoy//bazel/foreign_cc:event",
    )

def _net_zlib():
    location = REPOSITORY_LOCATIONS["net_zlib"]

    http_archive(
        name = "net_zlib",
        build_file_content = BUILD_ALL_CONTENT,
        **location
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

def _com_google_cel_cpp():
    _repository_impl("com_google_cel_cpp")

def _com_github_nghttp2_nghttp2():
    location = REPOSITORY_LOCATIONS["com_github_nghttp2_nghttp2"]
    http_archive(
        name = "com_github_nghttp2_nghttp2",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:nghttp2.patch"],
        **location
    )
    native.bind(
        name = "nghttp2",
        actual = "@envoy//bazel/foreign_cc:nghttp2",
    )

def _io_opentracing_cpp():
    _repository_impl(
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
    location = REPOSITORY_LOCATIONS["com_lightstep_tracer_cpp"]
    _repository_impl("com_lightstep_tracer_cpp")
    http_archive(
        name = "com_lightstep_tracer_cpp",
        patch_args = ["-p0"],
        patches = ["@envoy//bazel/foreign_cc:com_lightstep_tracer_cpp.patch"],
        **location
    )
    native.bind(
        name = "lightstep",
        actual = "@com_lightstep_tracer_cpp//:lightstep_tracer",
    )

def _com_github_datadog_dd_opentracing_cpp():
    _repository_impl("com_github_datadog_dd_opentracing_cpp")
    _repository_impl(
        name = "com_github_msgpack_msgpack_c",
        build_file = "@com_github_datadog_dd_opentracing_cpp//:bazel/external/msgpack.BUILD",
    )
    native.bind(
        name = "dd_opentracing_cpp",
        actual = "@com_github_datadog_dd_opentracing_cpp//:dd_opentracing_cpp",
    )

def _com_github_tencent_rapidjson():
    _repository_impl(
        name = "com_github_tencent_rapidjson",
        build_file = "@envoy//bazel/external:rapidjson.BUILD",
    )
    native.bind(
        name = "rapidjson",
        actual = "@com_github_tencent_rapidjson//:rapidjson",
    )

def _com_github_nodejs_http_parser():
    _repository_impl(
        name = "com_github_nodejs_http_parser",
        build_file = "@envoy//bazel/external:http-parser.BUILD",
    )
    native.bind(
        name = "http_parser",
        actual = "@com_github_nodejs_http_parser//:http_parser",
    )

def _com_google_googletest():
    _repository_impl("com_google_googletest")
    native.bind(
        name = "googletest",
        actual = "@com_google_googletest//:gtest",
    )

# TODO(jmarantz): replace the use of bind and external_deps with just
# the direct Bazel path at all sites.  This will make it easier to
# pull in more bits of abseil as needed, and is now the preferred
# method for pure Bazel deps.
def _com_google_absl():
    _repository_impl("com_google_absl")
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

def _com_google_protobuf():
    _repository_impl(
        "com_google_protobuf",
        # The patch includes
        # https://github.com/protocolbuffers/protobuf/pull/6333 and also uses
        # foreign_cc build for zlib as its dependency.
        # TODO(asraa): remove this when protobuf 3.10 is released.
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:protobuf.patch"],
    )

    # Needed for cc_proto_library, Bazel doesn't support aliases today for repos,
    # see https://groups.google.com/forum/#!topic/bazel-discuss/859ybHQZnuI and
    # https://github.com/bazelbuild/bazel/issues/3219.
    _repository_impl(
        "com_google_protobuf_cc",
        repository_key = "com_google_protobuf",
        # The patch includes
        # https://github.com/protocolbuffers/protobuf/pull/6333 and also uses
        # foreign_cc build for zlib as its dependency.
        # TODO(asraa): remove this when protobuf 3.10 is released.
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:protobuf.patch"],
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
        actual = "@com_google_protobuf_cc//:protoc",
    )

    # Needed for `bazel fetch` to work with @com_google_protobuf
    # https://github.com/google/protobuf/blob/v3.6.1/util/python/BUILD#L6-L9
    native.bind(
        name = "python_headers",
        actual = "@com_google_protobuf//util/python:python_headers",
    )

def _io_opencensus_cpp():
    location = REPOSITORY_LOCATIONS["io_opencensus_cpp"]
    http_archive(
        name = "io_opencensus_cpp",
        **location
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
    location = REPOSITORY_LOCATIONS["com_github_curl"]
    http_archive(
        name = "com_github_curl",
        build_file_content = BUILD_ALL_CONTENT + """
cc_library(name = "curl", visibility = ["//visibility:public"], deps = ["@envoy//bazel/foreign_cc:curl"])
""",
        **location
    )
    native.bind(
        name = "curl",
        actual = "@envoy//bazel/foreign_cc:curl",
    )

def _com_googlesource_quiche():
    location = REPOSITORY_LOCATIONS["com_googlesource_quiche"]
    genrule_repository(
        name = "com_googlesource_quiche",
        urls = location["urls"],
        sha256 = location["sha256"],
        genrule_cmd_file = "@envoy//bazel/external:quiche.genrule_cmd",
        build_file = "@envoy//bazel/external:quiche.BUILD",
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

def _org_llvm_releases_compiler_rt():
    _repository_impl(
        name = "org_llvm_releases_compiler_rt",
        build_file = "@envoy//bazel/external:compiler_rt.BUILD",
    )

def _fuzzit_linux():
    _repository_impl(
        name = "fuzzit_linux",
        build_file_content = "exports_files([\"fuzzit\"])",
    )

def _com_github_grpc_grpc():
    _repository_impl(
        "com_github_grpc_grpc",
        patches = [
            # Workaround for https://github.com/envoyproxy/envoy/issues/7863
            "@envoy//bazel:grpc-protoinfo-1.patch",
            "@envoy//bazel:grpc-protoinfo-2.patch",
            # Pre-integration of https://github.com/grpc/grpc/pull/19860
            "@envoy//bazel:grpc-protoinfo-3.patch",
            # Pre-integration of https://github.com/grpc/grpc/pull/18950
            "@envoy//bazel:grpc-rename-gettid.patch",
        ],
        patch_args = ["-p1"],
    )

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

def _com_github_nanopb_nanopb():
    _repository_impl(
        name = "com_github_nanopb_nanopb",
        build_file = "@com_github_grpc_grpc//third_party:nanopb.BUILD",
    )

    native.bind(
        name = "nanopb",
        actual = "@com_github_nanopb_nanopb//:nanopb",
    )

def _com_github_google_jwt_verify():
    _repository_impl("com_github_google_jwt_verify")

    native.bind(
        name = "jwt_verify_lib",
        actual = "@com_github_google_jwt_verify//:jwt_verify_lib",
    )

def _com_github_luajit_luajit():
    location = REPOSITORY_LOCATIONS["com_github_luajit_luajit"]
    http_archive(
        name = "com_github_luajit_luajit",
        build_file_content = BUILD_ALL_CONTENT,
        patches = ["@envoy//bazel/foreign_cc:luajit.patch"],
        patch_args = ["-p1"],
        patch_cmds = ["chmod u+x build.py"],
        **location
    )

    native.bind(
        name = "luajit",
        actual = "@envoy//bazel/foreign_cc:luajit",
    )

def _com_github_gperftools_gperftools():
    location = REPOSITORY_LOCATIONS["com_github_gperftools_gperftools"]
    http_archive(
        name = "com_github_gperftools_gperftools",
        build_file_content = BUILD_ALL_CONTENT,
        patch_cmds = ["./autogen.sh"],
        **location
    )

    native.bind(
        name = "gperftools",
        actual = "@envoy//bazel/foreign_cc:gperftools",
    )

def _foreign_cc_dependencies():
    _repository_impl("rules_foreign_cc")

def _rules_proto_dependencies():
    _repository_impl("rules_proto")

def _is_linux(ctxt):
    return ctxt.os.name == "linux"

def _is_arch(ctxt, arch):
    res = ctxt.execute(["uname", "-m"])
    return arch in res.stdout

def _is_linux_ppc(ctxt):
    return _is_linux(ctxt) and _is_arch(ctxt, "ppc")

def _is_linux_x86_64(ctxt):
    return _is_linux(ctxt) and _is_arch(ctxt, "x86_64")

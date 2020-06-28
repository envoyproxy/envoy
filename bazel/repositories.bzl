load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load(":dev_binding.bzl", "envoy_dev_binding")
load(":genrule_repository.bzl", "genrule_repository")
load("@envoy_api//bazel:envoy_http_archive.bzl", "envoy_http_archive")
load(":repository_locations.bzl", "DEPENDENCY_ANNOTATIONS", "DEPENDENCY_REPOSITORIES", "USE_CATEGORIES", "USE_CATEGORIES_WITH_CPE_OPTIONAL")
load("@com_google_googleapis//:repository_rules.bzl", "switched_rules_by_language")

PPC_SKIP_TARGETS = ["envoy.filters.http.lua"]

WINDOWS_SKIP_TARGETS = [
    "envoy.filters.http.lua",
    "envoy.tracers.dynamic_ot",
    "envoy.tracers.lightstep",
    "envoy.tracers.datadog",
    "envoy.tracers.opencensus",
]

# Make all contents of an external repository accessible under a filegroup.  Used for external HTTP
# archives, e.g. cares.
BUILD_ALL_CONTENT = """filegroup(name = "all", srcs = glob(["**"]), visibility = ["//visibility:public"])"""

# Method for verifying content of the DEPENDENCY_REPOSITORIES defined in bazel/repository_locations.bzl
# Verification is here so that bazel/repository_locations.bzl can be loaded into other tools written in Python,
# and as such needs to be free of bazel specific constructs.
def _repository_locations():
    locations = dict(DEPENDENCY_REPOSITORIES)
    for key, location in locations.items():
        if "sha256" not in location or len(location["sha256"]) == 0:
            fail("SHA256 missing for external dependency " + str(location["urls"]))

        if "use_category" not in location:
            fail("The 'use_category' attribute must be defined for external dependecy " + str(location["urls"]))

        if "cpe" not in location and not [category for category in USE_CATEGORIES_WITH_CPE_OPTIONAL if category in location["use_category"]]:
            fail("The 'cpe' attribute must be defined for external dependecy " + str(location["urls"]))

        for category in location["use_category"]:
            if category not in USE_CATEGORIES:
                fail("Unknown use_category value '" + category + "' for dependecy " + str(location["urls"]))

    return locations

REPOSITORY_LOCATIONS = _repository_locations()

# To initialize http_archive REPOSITORY_LOCATIONS dictionaries must be stripped of annotations.
# See repository_locations.bzl for the list of annotation attributes.
def _get_location(dependency):
    stripped = dict(REPOSITORY_LOCATIONS[dependency])
    for attribute in DEPENDENCY_ANNOTATIONS:
        stripped.pop(attribute, None)
    return stripped

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

# Python dependencies.
def _python_deps():
    # TODO(htuch): convert these to pip3_import.
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
        name = "six",
        build_file = "@com_google_protobuf//third_party:six.BUILD",
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
        _repository_impl(
            name = "io_bazel_rules_go",
            # TODO(wrowe, sunjayBhatia): remove when Windows RBE supports batch file invocation
            patch_args = ["-p1"],
            patches = ["@envoy//bazel:rules_go.patch"],
        )
        _repository_impl("bazel_gazelle")

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
    _com_github_gperftools_gperftools()
    _com_github_grpc_grpc()
    _com_github_jbeder_yaml_cpp()
    _com_github_libevent_libevent()
    _com_github_luajit_luajit()
    _com_github_moonjit_moonjit()
    _com_github_nghttp2_nghttp2()
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
    _upb()
    _repository_impl("com_googlesource_code_re2")
    _com_google_cel_cpp()
    _repository_impl("bazel_toolchains")
    _repository_impl("bazel_compdb")
    _repository_impl("envoy_build_tools")
    _repository_impl("rules_cc")

    # Unconditional, since we use this only for compiler-agnostic fuzzing utils.
    _org_llvm_releases_compiler_rt()

    _python_deps()
    _cc_deps()
    _go_deps(skip_targets)
    _kafka_deps()

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
    location = _get_location("com_github_c_ares_c_ares")
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

def _com_github_mirror_tclap():
    _repository_impl(
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
    location = _get_location("com_github_google_benchmark")
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
    location = _get_location("com_github_jbeder_yaml_cpp")
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
    location = _get_location("com_github_libevent_libevent")
    http_archive(
        name = "com_github_libevent_libevent",
        build_file_content = BUILD_ALL_CONTENT,
        **location
    )
    native.bind(
        name = "event",
        actual = "@envoy//bazel/foreign_cc:event",
    )

def _net_zlib():
    _repository_impl(
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

def _com_google_cel_cpp():
    _repository_impl("com_google_cel_cpp")

def _com_github_nghttp2_nghttp2():
    location = _get_location("com_github_nghttp2_nghttp2")
    http_archive(
        name = "com_github_nghttp2_nghttp2",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p1"],
        # This patch cannot be picked up due to ABI rules. Better
        # solve is likely at the next version-major. Discussion at;
        # https://github.com/nghttp2/nghttp2/pull/1395
        # https://github.com/envoyproxy/envoy/pull/8572#discussion_r334067786
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
    _repository_impl("com_lightstep_tracer_cpp")
    native.bind(
        name = "lightstep",
        actual = "@com_lightstep_tracer_cpp//:manual_tracer_lib",
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
    _repository_impl("rules_python")
    _repository_impl(
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
    location = _get_location("io_opencensus_cpp")
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
    location = _get_location("com_github_curl")
    http_archive(
        name = "com_github_curl",
        build_file_content = BUILD_ALL_CONTENT + """
cc_library(name = "curl", visibility = ["//visibility:public"], deps = ["@envoy//bazel/foreign_cc:curl"])
""",
        patches = ["@envoy//bazel/foreign_cc:curl-revert-cmake-minreqver.patch"],
        patch_args = ["-p1"],
        **location
    )
    native.bind(
        name = "curl",
        actual = "@envoy//bazel/foreign_cc:curl",
    )

def _com_googlesource_chromium_v8():
    location = _get_location("com_googlesource_chromium_v8")
    genrule_repository(
        name = "com_googlesource_chromium_v8",
        genrule_cmd_file = "@envoy//bazel/external:wee8.genrule_cmd",
        build_file = "@envoy//bazel/external:wee8.BUILD",
        patches = ["@envoy//bazel/external:wee8.patch"],
        **location
    )
    native.bind(
        name = "wee8",
        actual = "@com_googlesource_chromium_v8//:wee8",
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
    _repository_impl(
        name = "com_googlesource_googleurl",
    )
    native.bind(
        name = "googleurl",
        actual = "@com_googlesource_googleurl//url:url",
    )

def _org_llvm_releases_compiler_rt():
    _repository_impl(
        name = "org_llvm_releases_compiler_rt",
        build_file = "@envoy//bazel/external:compiler_rt.BUILD",
    )

def _com_github_grpc_grpc():
    _repository_impl("com_github_grpc_grpc")
    _repository_impl("build_bazel_rules_apple")

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

def _upb():
    _repository_impl(
        name = "upb",
        patches = ["@envoy//bazel:upb.patch"],
        patch_args = ["-p1"],
    )

    native.bind(
        name = "upb_lib",
        actual = "@upb//:upb",
    )

def _com_github_google_jwt_verify():
    _repository_impl("com_github_google_jwt_verify")

    native.bind(
        name = "jwt_verify_lib",
        actual = "@com_github_google_jwt_verify//:jwt_verify_lib",
    )

def _com_github_luajit_luajit():
    location = _get_location("com_github_luajit_luajit")
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

def _com_github_moonjit_moonjit():
    location = _get_location("com_github_moonjit_moonjit")
    http_archive(
        name = "com_github_moonjit_moonjit",
        build_file_content = BUILD_ALL_CONTENT,
        patches = ["@envoy//bazel/foreign_cc:moonjit.patch"],
        patch_args = ["-p1"],
        patch_cmds = ["chmod u+x build.py"],
        **location
    )

    native.bind(
        name = "moonjit",
        actual = "@envoy//bazel/foreign_cc:moonjit",
    )

def _com_github_gperftools_gperftools():
    location = _get_location("com_github_gperftools_gperftools")
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
    http_archive(
        name = "kafka_source",
        build_file_content = KAFKASOURCE_BUILD_CONTENT,
        patches = ["@envoy//bazel/external:kafka_int32.patch"],
        **_get_location("kafka_source")
    )

    # This archive provides Kafka (and Zookeeper) binaries, that are used during Kafka integration
    # tests.
    http_archive(
        name = "kafka_server_binary",
        build_file_content = BUILD_ALL_CONTENT,
        **_get_location("kafka_server_binary")
    )

    # This archive provides Kafka client in Python, so we can use it to interact with Kafka server
    # during interation tests.
    http_archive(
        name = "kafka_python_client",
        build_file_content = BUILD_ALL_CONTENT,
        **_get_location("kafka_python_client")
    )

def _foreign_cc_dependencies():
    _repository_impl("rules_foreign_cc")

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

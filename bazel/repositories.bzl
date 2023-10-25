load(":dev_binding.bzl", "envoy_dev_binding")
load("@envoy_api//bazel:envoy_http_archive.bzl", "envoy_http_archive")
load("@envoy_api//bazel:external_deps.bzl", "load_repository_locations")
load(":repository_locations.bzl", "PROTOC_VERSIONS", "REPOSITORY_LOCATIONS_SPEC")
load("@com_google_googleapis//:repository_rules.bzl", "switched_rules_by_language")

PPC_SKIP_TARGETS = ["envoy.filters.http.lua"]

WINDOWS_SKIP_TARGETS = [
    "envoy.extensions.http.cache.file_system_http_cache",
    "envoy.filters.http.file_system_buffer",
    "envoy.filters.http.language",
    "envoy.filters.http.sxg",
    "envoy.tracers.dynamic_ot",
    "envoy.tracers.datadog",
    "envoy.tracers.opencensus",
]

NO_HTTP3_SKIP_TARGETS = [
    "envoy.quic.crypto_stream.server.quiche",
    "envoy.quic.deterministic_connection_id_generator",
    "envoy.quic.crypto_stream.server.quiche",
    "envoy.quic.proof_source.filter_chain",
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

_default_envoy_build_config = repository_rule(
    implementation = _default_envoy_build_config_impl,
    attrs = {
        "config": attr.label(default = "@envoy//source/extensions:extensions_build_config.bzl"),
    },
)

def _envoy_repo_impl(repository_ctx):
    """This provides information about the Envoy repository

    You can access the current project and api versions and the path to the repository in
    .bzl/BUILD files as follows:

    ```starlark
    load("@envoy_repo//:version.bzl", "VERSION", "API_VERSION")
    ```

    `*VERSION` can be used to derive version-specific rules and can be passed
    to the rules.

    The `VERSION`s and also the local `PATH` to the repo can be accessed in
    python libraries/binaries. By adding `@envoy_repo` to `deps` they become
    importable through the `envoy_repo` namespace.

    As the `PATH` is local to the machine, it is generally only useful for
    jobs that will run locally.

    This can be useful, for example, for bazel run jobs to run bazel queries that cannot be run
    within the constraints of a `genquery`, or that otherwise need access to the repository
    files.

    Project and repo data can be accessed in JSON format using `@envoy_repo//:project`, eg:

    ```starlark
    load("@aspect_bazel_lib//lib:jq.bzl", "jq")

    jq(
        name = "project_version",
        srcs = ["@envoy_repo//:data"],
        out = "version.txt",
        args = ["-r"],
        filter = ".version",
    )

    ```

    """
    repo_version_path = repository_ctx.path(repository_ctx.attr.envoy_version)
    api_version_path = repository_ctx.path(repository_ctx.attr.envoy_api_version)
    version = repository_ctx.read(repo_version_path).strip()
    api_version = repository_ctx.read(api_version_path).strip()
    repository_ctx.file("version.bzl", "VERSION = '%s'\nAPI_VERSION = '%s'" % (version, api_version))
    repository_ctx.file("path.bzl", "PATH = '%s'" % repo_version_path.dirname)
    repository_ctx.file("__init__.py", "PATH = '%s'\nVERSION = '%s'\nAPI_VERSION = '%s'" % (repo_version_path.dirname, version, api_version))
    repository_ctx.file("WORKSPACE", "")
    repository_ctx.file("BUILD", '''
load("@rules_python//python:defs.bzl", "py_library")
load("@envoy//tools/base:envoy_python.bzl", "envoy_entry_point")
load("//:path.bzl", "PATH")

py_library(
    name = "envoy_repo",
    srcs = ["__init__.py"],
    visibility = ["//visibility:public"],
)

envoy_entry_point(
    name = "get_project_json",
    pkg = "envoy.base.utils",
    script = "envoy.project_data",
    init_data = [":__init__.py"],
)

genrule(
    name = "project",
    outs = ["project.json"],
    cmd = """
    $(location :get_project_json) $$(dirname $(location @envoy//:VERSION.txt)) > $@
    """,
    tools = [
        ":get_project_json",
        "@envoy//:VERSION.txt",
        "@envoy//changelogs",
    ],
    visibility = ["//visibility:public"],
)

envoy_entry_point(
    name = "release",
    args = [
        "release",
        PATH,
    ],
    pkg = "envoy.base.utils",
    script = "envoy.project",
    init_data = [":__init__.py"],
)

envoy_entry_point(
    name = "dev",
    args = [
        "dev",
        PATH,
    ],
    pkg = "envoy.base.utils",
    script = "envoy.project",
    init_data = [":__init__.py"],
)

envoy_entry_point(
    name = "sync",
    args = [
        "sync",
        PATH,
    ],
    pkg = "envoy.base.utils",
    script = "envoy.project",
    init_data = [":__init__.py"],
)

envoy_entry_point(
    name = "publish",
    args = [
        "publish",
        PATH,
    ],
    pkg = "envoy.base.utils",
    script = "envoy.project",
    init_data = [":__init__.py"],
)

envoy_entry_point(
    name = "trigger",
    args = [
        "trigger",
        PATH,
    ],
    pkg = "envoy.base.utils",
    script = "envoy.project",
    init_data = [":__init__.py"],
)

''')

_envoy_repo = repository_rule(
    implementation = _envoy_repo_impl,
    attrs = {
        "envoy_version": attr.label(default = "@envoy//:VERSION.txt"),
        "envoy_api_version": attr.label(default = "@envoy//:API_VERSION.txt"),
    },
)

def envoy_repo():
    if "envoy_repo" not in native.existing_rules().keys():
        _envoy_repo(name = "envoy_repo")

# Bazel native C++ dependencies. For the dependencies that doesn't provide autoconf/automake builds.
def _cc_deps():
    external_http_archive("grpc_httpjson_transcoding")
    external_http_archive("com_google_protoconverter")
    external_http_archive("com_google_protofieldextraction")
    external_http_archive("ocp")

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
    external_http_archive(
        "rules_rust",
        patches = ["@envoy//bazel:rules_rust.patch"],
    )

def envoy_dependencies(skip_targets = []):
    # Add a binding for repository variables.
    envoy_repo()

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

    # The long repo names (`com_github_fmtlib_fmt` instead of `fmtlib`) are
    # semi-standard in the Bazel community, intended to avoid both duplicate
    # dependencies and name conflicts.
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
    _com_github_gperftools_gperftools()
    _com_github_grpc_grpc()
    _com_github_unicode_org_icu()
    _com_github_intel_ipp_crypto_crypto_mb()
    _com_github_intel_qatlib()
    _com_github_jbeder_yaml_cpp()
    _com_github_libevent_libevent()
    _com_github_luajit_luajit()
    _com_github_nghttp2_nghttp2()
    _com_github_skyapm_cpp2sky()
    _com_github_alibaba_hessian2_codec()
    _com_github_tencent_rapidjson()
    _com_github_nlohmann_json()
    _com_github_ncopa_suexec()
    _com_google_absl()
    _com_google_googletest()
    _com_google_protobuf()
    _io_opencensus_cpp()
    _com_github_curl()
    _com_github_envoyproxy_sqlparser()
    _v8()
    _com_googlesource_chromium_base_trace_event_common()
    _com_github_google_quiche()
    _com_googlesource_googleurl()
    _io_hyperscan()
    _io_opentracing_cpp()
    _net_colm_open_source_colm()
    _net_colm_open_source_ragel()
    _net_zlib()
    _intel_dlb()
    _com_github_zlib_ng_zlib_ng()
    _org_boost()
    _org_brotli()
    _com_github_facebook_zstd()
    _re2()
    _upb()
    _proxy_wasm_cpp_sdk()
    _proxy_wasm_cpp_host()
    _emsdk()
    _rules_fuzzing()
    external_http_archive("proxy_wasm_rust_sdk")
    _com_google_cel_cpp()
    _com_github_google_perfetto()
    _utf8_range()
    _rules_ruby()
    external_http_archive("com_github_google_flatbuffers")
    external_http_archive("bazel_toolchains")
    external_http_archive("bazel_compdb")
    external_http_archive("envoy_build_tools")
    _com_github_maxmind_libmaxminddb()

    # TODO(keith): Remove patch when we update rules_pkg
    external_http_archive(
        "rules_pkg",
        patches = ["@envoy//bazel:rules_pkg.patch"],
    )
    external_http_archive("com_github_aignas_rules_shellcheck")
    external_http_archive("aspect_bazel_lib")
    _com_github_fdio_vpp_vcl()

    # Unconditional, since we use this only for compiler-agnostic fuzzing utils.
    _org_llvm_releases_compiler_rt()

    _cc_deps()
    _go_deps(skip_targets)
    _rust_deps()
    _kafka_deps()

    _org_llvm_llvm()
    _com_github_wamr()
    _com_github_wavm_wavm()
    _com_github_wasmtime()
    _com_github_wasm_c_api()

    switched_rules_by_language(
        name = "com_google_googleapis_imports",
        cc = True,
        go = True,
        grpc = True,
        rules_override = {
            "py_proto_library": ["@envoy_api//bazel:api_build_system.bzl", ""],
        },
    )

def _boringssl():
    external_http_archive(
        name = "boringssl",
        patch_args = ["-p1"],
        patches = [
            "@envoy//bazel:boringssl_static.patch",
        ],
    )

def _boringssl_fips():
    external_http_archive(
        name = "boringssl_fips",
        build_file = "@envoy//bazel/external:boringssl_fips.BUILD",
    )

def _com_github_openhistogram_libcircllhist():
    external_http_archive(
        name = "com_github_openhistogram_libcircllhist",
        build_file = "@envoy//bazel/external:libcircllhist.BUILD",
    )

def _com_github_axboe_liburing():
    external_http_archive(
        name = "com_github_axboe_liburing",
        build_file_content = BUILD_ALL_CONTENT,
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
        # Patch removes from CMakeLists.txt instructions to
        # to create dynamic *.so library target. Linker fails when linking
        # with boringssl_fips library. Envoy uses only static library
        # anyways, so created dynamic library would not be used anyways.
        patches = [
            "@envoy//bazel/foreign_cc:ipp-crypto-skip-dynamic-lib.patch",
            "@envoy//bazel/foreign_cc:ipp-crypto-bn2lebinpad.patch",
        ],
        patch_args = ["-p1"],
        build_file_content = BUILD_ALL_CONTENT,
    )

def _com_github_intel_qatlib():
    external_http_archive(
        name = "com_github_intel_qatlib",
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

def _net_zlib():
    external_http_archive(
        name = "net_zlib",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:zlib.patch"],
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

def _com_github_facebook_zstd():
    external_http_archive(
        name = "com_github_facebook_zstd",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _com_google_cel_cpp():
    external_http_archive(
        "com_google_cel_cpp",
        patches = ["@envoy//bazel:cel-cpp.patch"],
        patch_args = ["-p1"],
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
        patches = ["@envoy//bazel/foreign_cc:nghttp2.patch"],
    )

def _io_hyperscan():
    external_http_archive(
        name = "io_hyperscan",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:hyperscan.patch"],
    )

def _io_opentracing_cpp():
    external_http_archive(
        name = "io_opentracing_cpp",
        patch_args = ["-p1"],
        # Workaround for LSAN false positive in https://github.com/envoyproxy/envoy/issues/7647
        patches = ["@envoy//bazel:io_opentracing_cpp.patch"],
    )

def _com_github_datadog_dd_trace_cpp():
    external_http_archive("com_github_datadog_dd_trace_cpp")

def _com_github_skyapm_cpp2sky():
    external_http_archive(
        name = "com_github_skyapm_cpp2sky",
    )
    external_http_archive(
        name = "skywalking_data_collect_protocol",
    )

def _com_github_tencent_rapidjson():
    external_http_archive(
        name = "com_github_tencent_rapidjson",
        build_file = "@envoy//bazel/external:rapidjson.BUILD",
    )

def _com_github_nlohmann_json():
    external_http_archive(
        name = "com_github_nlohmann_json",
        build_file = "@envoy//bazel/external:json.BUILD",
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
    )
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
        name = "abseil_btree",
        actual = "@com_google_absl//absl/container:btree",
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
    native.bind(
        name = "abseil_cleanup",
        actual = "@com_google_absl//absl/cleanup:cleanup",
    )

def _com_google_protobuf():
    external_http_archive(
        name = "rules_python",
    )

    for platform in PROTOC_VERSIONS:
        # Ideally we dont use a private build artefact as done here.
        # If `rules_proto` implements protoc toolchains in the future (currently it
        # is there, but is empty) we should remove these and use that rule
        # instead.
        external_http_archive(
            "com_google_protobuf_protoc_%s" % platform,
            build_file = "@rules_proto//proto/private:BUILD.protoc",
        )

    external_http_archive(
        "com_google_protobuf",
        patches = ["@envoy//bazel:protobuf.patch"],
        patch_args = ["-p1"],
    )

    # Needed for `bazel fetch` to work with @com_google_protobuf
    # https://github.com/google/protobuf/blob/v3.6.1/util/python/BUILD#L6-L9
    native.bind(
        name = "python_headers",
        actual = "//bazel:python_headers",
    )

def _io_opencensus_cpp():
    external_http_archive(
        name = "io_opencensus_cpp",
    )

def _com_github_curl():
    # Used by OpenCensus Zipkin exporter.
    external_http_archive(
        name = "com_github_curl",
        build_file_content = BUILD_ALL_CONTENT + """
cc_library(name = "curl", visibility = ["//visibility:public"], deps = ["@envoy//bazel/foreign_cc:curl"])
""",
        # Patch curl 7.74.0 and later due to CMake's problematic implementation of policy `CMP0091`
        # and introduction of libidn2 dependency which is inconsistently available and must
        # not be a dynamic dependency on linux.
        # Upstream patches submitted: https://github.com/curl/curl/pull/6050 & 6362
        # TODO(https://github.com/envoyproxy/envoy/issues/11816): This patch is obsoleted
        # by elimination of the curl dependency.
        patches = ["@envoy//bazel/foreign_cc:curl.patch"],
        patch_args = ["-p1"],
    )

def _v8():
    external_http_archive(
        name = "v8",
        patches = [
            "@envoy//bazel:v8.patch",
            "@envoy//bazel:v8_include.patch",
        ],
        patch_args = ["-p1"],
    )

    # Required by proxy-wasm-cpp-host
    native.bind(
        name = "wee8",
        actual = "@v8//:wee8",
    )

def _com_googlesource_chromium_base_trace_event_common():
    external_http_archive(
        name = "com_googlesource_chromium_base_trace_event_common",
        build_file = "@v8//:bazel/BUILD.trace_event_common",
    )

def _com_github_google_quiche():
    external_http_archive(
        name = "com_github_google_quiche",
        patch_cmds = ["find quiche/ -type f -name \"*.bazel\" -delete"],
        build_file = "@envoy//bazel/external:quiche.BUILD",
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
    external_http_archive(
        name = "com_github_grpc_grpc",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:grpc.patch"],
    )
    external_http_archive("build_bazel_rules_apple")

    # Rebind some stuff to match what the gRPC Bazel is expecting.
    native.bind(
        name = "protobuf_headers",
        actual = "@com_google_protobuf//:protobuf_headers",
    )
    native.bind(
        name = "libssl",
        actual = "@envoy//bazel:boringssl",
    )
    native.bind(
        name = "libcrypto",
        actual = "@envoy//bazel:boringcrypto",
    )
    native.bind(
        name = "cares",
        actual = "@envoy//bazel/foreign_cc:ares",
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
        name = "upb_collections_lib",
        actual = "@upb//:collections",
    )

    native.bind(
        name = "upb_lib_descriptor",
        actual = "@upb//:descriptor_upb_proto",
    )

    native.bind(
        name = "upb_lib_descriptor_reflection",
        actual = "@upb//:descriptor_upb_proto_reflection",
    )

    native.bind(
        name = "upb_textformat_lib",
        actual = "@upb//:textformat",
    )

    native.bind(
        name = "upb_json_lib",
        actual = "@upb//:json",
    )

    native.bind(
        name = "upb_reflection",
        actual = "@upb//:reflection",
    )

    native.bind(
        name = "upb_generated_code_support__only_for_generated_code_do_not_use__i_give_permission_to_break_me",
        actual = "@upb//:generated_code_support__only_for_generated_code_do_not_use__i_give_permission_to_break_me",
    )

def _re2():
    external_http_archive("com_googlesource_code_re2")

def _upb():
    external_http_archive(
        name = "upb",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:upb.patch"],
    )

    # Needed by grpc
    native.bind(
        name = "upb_lib",
        actual = "@upb//:upb",
    )

def _proxy_wasm_cpp_sdk():
    external_http_archive(name = "proxy_wasm_cpp_sdk")

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
    external_http_archive("com_github_google_jwt_verify")

def _com_github_luajit_luajit():
    external_http_archive(
        name = "com_github_luajit_luajit",
        build_file_content = BUILD_ALL_CONTENT,
        patches = ["@envoy//bazel/foreign_cc:luajit.patch"],
        patch_args = ["-p1"],
        patch_cmds = ["chmod u+x build.py"],
    )

def _com_github_google_tcmalloc():
    external_http_archive(
        name = "com_github_google_tcmalloc",
    )

def _com_github_gperftools_gperftools():
    external_http_archive(
        name = "com_github_gperftools_gperftools",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _org_llvm_llvm():
    external_http_archive(
        name = "org_llvm_llvm",
        build_file_content = BUILD_ALL_CONTENT,
        patch_args = ["-p1"],
        patches = ["@envoy//bazel/foreign_cc:llvm.patch"],
    )

def _com_github_wamr():
    external_http_archive(
        name = "com_github_wamr",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _com_github_wavm_wavm():
    external_http_archive(
        name = "com_github_wavm_wavm",
        build_file_content = BUILD_ALL_CONTENT,
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

    # This isn't needed in builds with a single Wasm engine, but "bazel query"
    # complains about a missing dependency, so point it at the regular target.
    native.bind(
        name = "prefixed_wasmtime",
        actual = "@com_github_wasm_c_api//:wasmtime_lib",
    )

def _intel_dlb():
    external_http_archive(
        name = "intel_dlb",
        build_file_content = """
filegroup(
    name = "libdlb",
    srcs = glob([
        "dlb/libdlb/**",
    ]),
    visibility = ["@envoy//contrib/dlb/source:__pkg__"],
)
""",
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
        name = "edenhill_librdkafka",
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

    # This archive provides Kafka client in Python, so we can use it to interact with Kafka server
    # during integration tests.
    external_http_archive(
        name = "kafka_python_client",
        build_file_content = BUILD_ALL_CONTENT,
    )

def _com_github_fdio_vpp_vcl():
    external_http_archive(
        name = "com_github_fdio_vpp_vcl",
        build_file_content = _build_all_content(exclude = ["**/*doc*/**", "**/examples/**", "**/plugins/**"]),
        patches = ["@envoy//bazel/foreign_cc:vpp_vcl.patch"],
    )

def _utf8_range():
    external_http_archive("utf8_range")

def _rules_ruby():
    external_http_archive("rules_ruby")

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

def _com_github_maxmind_libmaxminddb():
    external_http_archive(
        name = "com_github_maxmind_libmaxminddb",
        build_file_content = BUILD_ALL_CONTENT,
    )

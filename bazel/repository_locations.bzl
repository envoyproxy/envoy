# Validation of content in this file is done on the bazel/repositories.bzl file to make it free of bazel
# constructs. This is to allow this file to be loaded into Python based build and maintenance tools.

# Envoy dependencies may be annotated with the following attributes:
DEPENDENCY_ANNOTATIONS = [
    # List of the categories describing how the dependency is being used. This attribute is used
    # for automatic tracking of security posture of Envoy's dependencies.
    # Possible values are documented in the USE_CATEGORIES list below.
    # This attribute is mandatory for each dependecy.
    "use_category",

    # Attribute specifying CPE (Common Platform Enumeration, see https://nvd.nist.gov/products/cpe) ID
    # of the dependency. The ID may be in v2.3 or v2.2 format, although v2.3 is prefferred. See
    # https://nvd.nist.gov/products/cpe for CPE format. Use single wildcard '*' for version and vector elements
    # i.e. 'cpe:2.3:a:nghttp2:nghttp2:*'. Use "N/A" for dependencies without CPE assigned.
    # This attribute is optional for components with use categories listed in the
    # USE_CATEGORIES_WITH_CPE_OPTIONAL
    "cpe",
]

# NOTE: If a dependency use case is either dataplane or controlplane, the other uses are not needed
# to be declared.
USE_CATEGORIES = [
    # This dependency is used in API protos.
    "api",
    # This dependency is used in build process.
    "build",
    # This dependency is used to process xDS requests.
    "controlplane",
    # This dependency is used in processing downstream or upstream requests.
    "dataplane",
    # This dependecy is used for logging, metrics or tracing. It may process unstrusted input.
    "observability",
    # This dependency does not handle untrusted data and is used for various utility purposes.
    "other",
    # This dependency is used for unit tests.
    "test",
]

# Components with these use categories are not required to specify the 'cpe' annotation.
USE_CATEGORIES_WITH_CPE_OPTIONAL = ["build", "other", "test"]

DEPENDENCY_REPOSITORIES_SPEC = dict(
    bazel_compdb = dict(
        project_name = "bazil-compilation-database",
        project_url = "https://github.com/grailbio/bazel-compilation-database",
        version = "0.4.5",
        sha256 = "bcecfd622c4ef272fd4ba42726a52e140b961c4eac23025f18b346c968a8cfb4",
        strip_prefix = "bazel-compilation-database-{version}",
        urls = ["https://github.com/grailbio/bazel-compilation-database/archive/{version}.tar.gz"],
        use_category = ["build"],
    ),
    bazel_gazelle = dict(
        project_name = "Gazelle",
        project_url = "https://github.com/bazelbuild/bazel-gazelle",
        version = "0.21.1",
        sha256 = "cdb02a887a7187ea4d5a27452311a75ed8637379a1287d8eeb952138ea485f7d",
        urls = ["https://github.com/bazelbuild/bazel-gazelle/releases/download/v{version}/bazel-gazelle-v{version}.tar.gz"],
        use_category = ["build"],
    ),
    bazel_toolchains = dict(
        project_name = "bazel-toolchains",
        project_url = "https://github.com/bazelbuild/bazel-toolchains",
        version = "3.4.1",
        sha256 = "7ebb200ed3ca3d1f7505659c7dfed01c4b5cb04c3a6f34140726fe22f5d35e86",
        strip_prefix = "bazel-toolchains-{version}",
        urls = [
            "https://github.com/bazelbuild/bazel-toolchains/releases/download/{version}/bazel-toolchains-{version}.tar.gz",
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/archive/{version}.tar.gz",
        ],
        use_category = ["build"],
    ),
    build_bazel_rules_apple = dict(
        project_name = "Apple Rules for Bazel",
        project_url = "https://github.com/bazelbuild/rules_apple",
        version = "0.19.0",
        sha256 = "7a7afdd4869bb201c9352eed2daf37294d42b093579b70423490c1b4d4f6ce42",
        urls = ["https://github.com/bazelbuild/rules_apple/releases/download/{version}/rules_apple.{version}.tar.gz"],
        use_category = ["build"],
    ),
    envoy_build_tools = dict(
        project_name = "envoy-build-tools",
        project_url = "https://github.com/envoyproxy/envoy-build-tools",
        version = "2d13ad4157997715a4939bd218a89c81c26ff28e",
        sha256 = "0dc8ce5eb645ae069ce710c1010975456f723ffd4fc788a03dacfcd0647b05b9",
        strip_prefix = "envoy-build-tools-{version}",
        # 2020-08-21
        urls = ["https://github.com/envoyproxy/envoy-build-tools/archive/{version}.tar.gz"],
        use_category = ["build"],
    ),
    boringssl = dict(
        project_name = "BoringSSL",
        project_url = "https://github.com/google/boringssl",
        version = "597b810379e126ae05d32c1d94b1a9464385acd0",
        sha256 = "1ea42456c020daf0a9b0f9e8d8bc3a403c9314f4f54230c617257af996cd5fa6",
        strip_prefix = "boringssl-{version}",
        # To update BoringSSL, which tracks Chromium releases:
        # 1. Open https://omahaproxy.appspot.com/ and note <current_version> of linux/stable release.
        # 2. Open https://chromium.googlesource.com/chromium/src/+/refs/tags/<current_version>/DEPS and note <boringssl_revision>.
        # 3. Find a commit in BoringSSL's "master-with-bazel" branch that merges <boringssl_revision>.
        #
        # chromium-85.0.4183.83
        # 2020-06-23
        urls = ["https://github.com/google/boringssl/archive/{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    boringssl_fips = dict(
        project_name = "BoringSSL (FIPS)",
        project_url = "https://boringssl.googlesource.com/boringssl/+/master/crypto/fipsmodule/FIPS.md",
        version = "fips-20190808",
        sha256 = "3b5fdf23274d4179c2077b5e8fa625d9debd7a390aac1d165b7e47234f648bb8",
        urls = ["https://commondatastorage.googleapis.com/chromium-boringssl-fips/boringssl-ae223d6138807a13006342edfeef32e813246b39.tar.xz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_google_absl = dict(
        project_name = "Abseil",
        project_url = "https://abseil.io/",
        version = "ce4bc927755fdf0ed03d679d9c7fa041175bb3cb",
        sha256 = "573baccd67aa591b8c7209bfb0c77e0d15633d77ced39d1ccbb1232828f7f7d9",
        strip_prefix = "abseil-cpp-{version}",
        # 2020-08-08
        urls = ["https://github.com/abseil/abseil-cpp/archive/{version}.tar.gz"],
        use_category = ["dataplane", "controlplane"],
        cpe = "N/A",
    ),
    com_github_apache_thrift = dict(
        project_name = "Apache Thrift",
        project_url = "http://thrift.apache.org/",
        version = "0.11.0",
        sha256 = "7d59ac4fdcb2c58037ebd4a9da5f9a49e3e034bf75b3f26d9fe48ba3d8806e6b",
        strip_prefix = "thrift-{version}",
        urls = ["https://files.pythonhosted.org/packages/c6/b4/510617906f8e0c5660e7d96fbc5585113f83ad547a3989b80297ac72a74c/thrift-{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:apache:thrift:*",
    ),
    com_github_c_ares_c_ares = dict(
        project_name = "c-ares",
        project_url = "https://c-ares.haxx.se/",
        version = "1.16.1",
        sha256 = "d08312d0ecc3bd48eee0a4cc0d2137c9f194e0a28de2028928c0f6cae85f86ce",
        strip_prefix = "c-ares-{version}",
        urls = ["https://github.com/c-ares/c-ares/releases/download/cares-1_16_1/c-ares-{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:c-ares_project:c-ares:*",
    ),
    com_github_circonus_labs_libcircllhist = dict(
        project_name = "libcircllhist",
        project_url = "https://github.com/circonus-labs/libcircllhist",
        # 2019-02-11
        version = "63a16dd6f2fc7bc841bb17ff92be8318df60e2e1",
        sha256 = "8165aa25e529d7d4b9ae849d3bf30371255a99d6db0421516abcff23214cdc2c",
        strip_prefix = "libcircllhist-{version}",
        urls = ["https://github.com/circonus-labs/libcircllhist/archive/{version}.tar.gz"],
        use_category = ["observability"],
        cpe = "N/A",
    ),
    com_github_cyan4973_xxhash = dict(
        project_name = "xxHash",
        project_url = "https://github.com/Cyan4973/xxHash",
        version = "0.7.3",
        sha256 = "952ebbf5b11fbf59ae5d760a562d1e9112278f244340ad7714e8556cbe54f7f7",
        strip_prefix = "xxHash-{version}",
        urls = ["https://github.com/Cyan4973/xxHash/archive/v{version}.tar.gz"],
        use_category = ["dataplane", "controlplane"],
        cpe = "N/A",
    ),
    com_github_envoyproxy_sqlparser = dict(
        project_name = "C++ SQL Parser Library",
        project_url = "https://github.com/envoyproxy/sql-parser",
        # 2020-06-10
        version = "3b40ba2d106587bdf053a292f7e3bb17e818a57f",
        sha256 = "96c10c8e950a141a32034f19b19cdeb1da48fe859cf96ae5e19f894f36c62c71",
        strip_prefix = "sql-parser-{version}",
        urls = ["https://github.com/envoyproxy/sql-parser/archive/{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_github_mirror_tclap = dict(
        project_name = "tclap",
        project_url = "http://tclap.sourceforge.net",
        version = "1-2-1",
        sha256 = "f0ede0721dddbb5eba3a47385a6e8681b14f155e1129dd39d1a959411935098f",
        strip_prefix = "tclap-tclap-{version}-release-final",
        urls = ["https://github.com/mirror/tclap/archive/tclap-{version}-release-final.tar.gz"],
        use_category = ["other"],
    ),
    com_github_fmtlib_fmt = dict(
        project_name = "fmt",
        project_url = "https://fmt.dev",
        version = "7.0.3",
        sha256 = "decfdf9ad274070fa85f26407b816f5a4d82205ae86bac1990be658d0795ea4d",
        strip_prefix = "fmt-{version}",
        urls = ["https://github.com/fmtlib/fmt/releases/download/{version}/fmt-{version}.zip"],
        use_category = ["observability"],
        cpe = "N/A",
    ),
    com_github_gabime_spdlog = dict(
        project_name = "spdlog",
        project_url = "https://github.com/gabime/spdlog",
        version = "1.7.0",
        sha256 = "f0114a4d3c88be9e696762f37a7c379619443ce9d668546c61b21d41affe5b62",
        strip_prefix = "spdlog-{version}",
        urls = ["https://github.com/gabime/spdlog/archive/v{version}.tar.gz"],
        use_category = ["observability"],
        cpe = "N/A",
    ),
    com_github_google_libprotobuf_mutator = dict(
        project_name = "libprotobuf-mutator",
        project_url = "https://github.com/google/libprotobuf-mutator",
        # 2020-08-18
        version = "8942a9ba43d8bb196230c321d46d6a137957a719",
        sha256 = "49a26dbe77c75f2eca1dd8a9fbdb31c4496d9af42df027ff57569c5a7a5d980d",
        strip_prefix = "libprotobuf-mutator-{version}",
        urls = ["https://github.com/google/libprotobuf-mutator/archive/{version}.tar.gz"],
        use_category = ["test"],
    ),
    com_github_gperftools_gperftools = dict(
        project_name = "gperftools",
        project_url = "https://github.com/gperftools/gperftools",
        version = "2.8",
        sha256 = "240deacdd628b6459671b83eb0c4db8e97baadf659f25b92e9a078d536bd513e",
        strip_prefix = "gperftools-{version}",
        urls = ["https://github.com/gperftools/gperftools/releases/download/gperftools-{version}/gperftools-{version}.tar.gz"],
        use_category = ["test"],
    ),
    com_github_grpc_grpc = dict(
        project_name = "gRPC",
        project_url = "https://grpc.io",
        # TODO(JimmyCYJ): Bump to release 1.27
        # This sha on grpc:v1.25.x branch is specifically chosen to fix gRPC STS call credential options.
        # 2020-02-11
        version = "d8f4928fa779f6005a7fe55a176bdb373b0f910f",
        sha256 = "bbc8f020f4e85ec029b047fab939b8c81f3d67254b5c724e1003a2bc49ddd123",
        strip_prefix = "grpc-{version}",
        urls = ["https://github.com/grpc/grpc/archive/{version}.tar.gz"],
        use_category = ["dataplane", "controlplane"],
        cpe = "cpe:2.3:a:grpc:grpc:*",
    ),
    com_github_luajit_luajit = dict(
        project_name = "LuaJIT",
        project_url = "https://luajit.org",
        version = "2.1.0-beta3",
        sha256 = "409f7fe570d3c16558e594421c47bdd130238323c9d6fd6c83dedd2aaeb082a8",
        strip_prefix = "LuaJIT-{version}",
        urls = ["https://github.com/LuaJIT/LuaJIT/archive/v{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_github_moonjit_moonjit = dict(
        project_name = "Moonjit",
        project_url = "https://github.com/moonjit/moonjit",
        version = "2.2.0",
        sha256 = "83deb2c880488dfe7dd8ebf09e3b1e7613ef4b8420de53de6f712f01aabca2b6",
        strip_prefix = "moonjit-{version}",
        urls = ["https://github.com/moonjit/moonjit/archive/{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_github_nghttp2_nghttp2 = dict(
        project_name = "Nghttp2",
        project_url = "https://nghttp2.org",
        version = "1.41.0",
        sha256 = "eacc6f0f8543583ecd659faf0a3f906ed03826f1d4157b536b4b385fe47c5bb8",
        strip_prefix = "nghttp2-{version}",
        urls = ["https://github.com/nghttp2/nghttp2/releases/download/v{version}/nghttp2-{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:nghttp2:nghttp2:*",
    ),
    io_opentracing_cpp = dict(
        project_name = "OpenTracing",
        project_url = "https://opentracing.io",
        version = "1.5.1",
        sha256 = "015c4187f7a6426a2b5196f0ccd982aa87f010cf61f507ae3ce5c90523f92301",
        strip_prefix = "opentracing-cpp-{version}",
        urls = ["https://github.com/opentracing/opentracing-cpp/archive/v{version}.tar.gz"],
        use_category = ["observability"],
        cpe = "N/A",
    ),
    com_lightstep_tracer_cpp = dict(
        project_name = "lightstep-tracer-cpp",
        project_url = "https://github.com/lightstep/lightstep-tracer-cpp",
        # 2020-08-24
        version = "1942b3f142e218ebc143a043f32e3278dafec9aa",
        sha256 = "3238921a8f578beb26c2215cd277e8f6752f3d29b020b881d60d96a240a38aed",
        strip_prefix = "lightstep-tracer-cpp-{version}",
        urls = ["https://github.com/lightstep/lightstep-tracer-cpp/archive/{version}.tar.gz"],
        use_category = ["observability"],
        cpe = "N/A",
    ),
    com_github_datadog_dd_opentracing_cpp = dict(
        project_name = "Datadog OpenTracing C++ Client",
        project_url = "https://github.com/DataDog/dd-opentracing-cpp",
        version = "1.1.5",
        sha256 = "b84fd2fb0bb0578af4901db31d1c0ae909b532a1016fe6534cbe31a6c3ad6924",
        strip_prefix = "dd-opentracing-cpp-{version}",
        urls = ["https://github.com/DataDog/dd-opentracing-cpp/archive/v{version}.tar.gz"],
        use_category = ["observability"],
        cpe = "N/A",
    ),
    com_github_google_benchmark = dict(
        project_name = "Benchmark",
        project_url = "https://github.com/google/benchmark",
        version = "1.5.1",
        sha256 = "23082937d1663a53b90cb5b61df4bcc312f6dee7018da78ba00dd6bd669dfef2",
        strip_prefix = "benchmark-{version}",
        urls = ["https://github.com/google/benchmark/archive/v{version}.tar.gz"],
        use_category = ["test"],
    ),
    com_github_libevent_libevent = dict(
        project_name = "libevent",
        project_url = "https://libevent.org",
        # This SHA includes the new "prepare" and "check" watchers, used for event loop performance
        # stats (see https://github.com/libevent/libevent/pull/793) and the fix for a race condition
        # in the watchers (see https://github.com/libevent/libevent/pull/802).
        # This also includes the fixes for https://github.com/libevent/libevent/issues/806
        # and https://github.com/lyft/envoy-mobile/issues/215.
        # This also includes the fixes for Phantom events with EV_ET (see
        # https://github.com/libevent/libevent/issues/984).
        # This also includes the wepoll backend for Windows (see
        # https://github.com/libevent/libevent/pull/1006)
        # TODO(adip): Update to v2.2 when it is released.
        # 2020-07-31
        version = "62c152d9a7cd264b993dad730c4163c6ede2e0a3",
        sha256 = "4c80e5fe044ce5f8055b20a2f141ee32ec2614000f3e95d2aa81611a4c8f5213",
        strip_prefix = "libevent-{version}",
        urls = ["https://github.com/libevent/libevent/archive/{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:libevent_project:libevent:*",
    ),
    net_zlib = dict(
        project_name = "zlib",
        project_url = "https://zlib.net",
        version = "79baebe50e4d6b73ae1f8b603f0ef41300110aa3",
        # Use the dev branch of zlib to resolve fuzz bugs and out of bound
        # errors resulting in crashes in zlib 1.2.11.
        # TODO(asraa): Remove when zlib > 1.2.11 is released.
        # 2019-04-14 development branch
        sha256 = "155a8f8c1a753fb05b16a1b0cc0a0a9f61a78e245f9e0da483d13043b3bcbf2e",
        strip_prefix = "zlib-{version}",
        urls = ["https://github.com/madler/zlib/archive/{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:gnu:zlib:*",
    ),
    com_github_zlib_ng_zlib_ng = dict(
        project_name = "zlib-ng",
        project_url = "https://github.com/zlib-ng/zlib-ng",
        version = "193d8fd7dfb7927facab7a3034daa27ad5b9df1c",
        sha256 = "5fe543e8d007b9e7b729f3d6b3a5ee1f9b68d0eef5f6af1393745a4dcd472a98",
        strip_prefix = "zlib-ng-193d8fd7dfb7927facab7a3034daa27ad5b9df1c",
        # 2020-08-16 develop branch.
        urls = ["https://github.com/zlib-ng/zlib-ng/archive/193d8fd7dfb7927facab7a3034daa27ad5b9df1c.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_github_jbeder_yaml_cpp = dict(
        project_name = "yaml-cpp",
        project_url = "https://github.com/jbeder/yaml-cpp",
        # 2020-07-28
        version = "98acc5a8874faab28b82c28936f4b400b389f5d6",
        sha256 = "79ab7069ef1c7c3632e7ffe095f7185d4c77b64d8035db3c085c239d4fe96d5f",
        strip_prefix = "yaml-cpp-{version}",
        urls = ["https://github.com/jbeder/yaml-cpp/archive/{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_github_msgpack_msgpack_c = dict(
        project_name = "msgpack for C/C++",
        project_url = "https://github.com/msgpack/msgpack-c",
        version = "3.3.0",
        sha256 = "6e114d12a5ddb8cb11f669f83f32246e484a8addd0ce93f274996f1941c1f07b",
        strip_prefix = "msgpack-{version}",
        urls = ["https://github.com/msgpack/msgpack-c/releases/download/cpp-{version}/msgpack-{version}.tar.gz"],
        use_category = ["observability"],
        cpe = "N/A",
    ),
    com_github_google_jwt_verify = dict(
        project_name = "jwt_verify_lib",
        project_url = "https://github.com/google/jwt_verify_lib",
        # 2020-07-09
        version = "7276a339af8426724b744216f619c99152f8c141",
        sha256 = "f1fde4f3ebb3b2d841332c7a02a4b50e0529a19709934c63bc6208d1bbe28fb1",
        strip_prefix = "jwt_verify_lib-{version}",
        urls = ["https://github.com/google/jwt_verify_lib/archive/{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_github_nodejs_http_parser = dict(
        project_name = "HTTP Parser",
        project_url = "https://github.com/nodejs/http-parser",
        version = "2.9.3",
        sha256 = "8fa0ab8770fd8425a9b431fdbf91623c4d7a9cdb842b9339289bd2b0b01b0d3d",
        strip_prefix = "http-parser-{version}",
        urls = ["https://github.com/nodejs/http-parser/archive/v{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:nodejs:node.js:*",
    ),
    com_github_pallets_jinja = dict(
        project_name = "https://palletsprojects.com/p/jinja",
        project_url = "Jinja",
        version = "2.10.3",
        sha256 = "db49236731373e4f3118af880eb91bb0aa6978bc0cf8b35760f6a026f1a9ffc4",
        strip_prefix = "jinja-{version}",
        urls = ["https://github.com/pallets/jinja/archive/{version}.tar.gz"],
        use_category = ["build"],
    ),
    com_github_pallets_markupsafe = dict(
        project_name = "MarkupSafe",
        project_url = "https://github.com/pallets/markupsafe",
        version = "2.0.0a1",
        sha256 = "2b0c5c2a067d9268813d55523bc513a12181cffb23b2f3d5618eb5d93776bad8",
        strip_prefix = "markupsafe-{version}/src",
        urls = ["https://github.com/pallets/markupsafe/archive/{version}.tar.gz"],
        use_category = ["build"],
    ),
    com_github_tencent_rapidjson = dict(
        project_name = "RapidJSON",
        project_url = "https://rapidjson.org",
        # Changes through 2019-12-02
        version = "dfbe1db9da455552f7a9ad5d2aea17dd9d832ac1",
        sha256 = "a2faafbc402394df0fa94602df4b5e4befd734aad6bb55dfef46f62fcaf1090b",
        strip_prefix = "rapidjson-{version}",
        urls = ["https://github.com/Tencent/rapidjson/archive/{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:tencent:rapidjson:*",
    ),
    com_github_twitter_common_lang = dict(
        project_name = "twitter.common.lang (Thrift)",
        project_url = "https://pypi.org/project/twitter.common.lang",
        version = "0.3.9",
        sha256 = "56d1d266fd4767941d11c27061a57bc1266a3342e551bde3780f9e9eb5ad0ed1",
        strip_prefix = "twitter.common.lang-{version}/src",
        urls = ["https://files.pythonhosted.org/packages/08/bc/d6409a813a9dccd4920a6262eb6e5889e90381453a5f58938ba4cf1d9420/twitter.common.lang-{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_github_twitter_common_rpc = dict(
        project_name = "twitter.common.rpc (Thrift)",
        project_url = "https://pypi.org/project/twitter.common.rpc",
        version = "0.3.9",
        sha256 = "0792b63fb2fb32d970c2e9a409d3d00633190a22eb185145fe3d9067fdaa4514",
        strip_prefix = "twitter.common.rpc-{version}/src",
        urls = ["https://files.pythonhosted.org/packages/be/97/f5f701b703d0f25fbf148992cd58d55b4d08d3db785aad209255ee67e2d0/twitter.common.rpc-{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_github_twitter_common_finagle_thrift = dict(
        project_name = "twitter.common.finagle-thrift",
        project_url = "https://pypi.org/project/twitter.common.finagle-thrift",
        version = "0.3.9",
        sha256 = "1e3a57d11f94f58745e6b83348ecd4fa74194618704f45444a15bc391fde497a",
        strip_prefix = "twitter.common.finagle-thrift-{version}/src",
        urls = ["https://files.pythonhosted.org/packages/f9/e7/4f80d582578f8489226370762d2cf6bc9381175d1929eba1754e03f70708/twitter.common.finagle-thrift-{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_google_googletest = dict(
        project_name = "Google Test",
        project_url = "https://github.com/google/googletest",
        version = "1.10.0",
        sha256 = "9dc9157a9a1551ec7a7e43daea9a694a0bb5fb8bec81235d8a1e6ef64c716dcb",
        strip_prefix = "googletest-release-{version}",
        urls = ["https://github.com/google/googletest/archive/release-{version}.tar.gz"],
        use_category = ["test"],
    ),
    com_google_protobuf = dict(
        project_name = "Protocol Buffers",
        project_url = "https://developers.google.com/protocol-buffers",
        version = "3.10.1",
        sha256 = "d7cfd31620a352b2ee8c1ed883222a0d77e44346643458e062e86b1d069ace3e",
        strip_prefix = "protobuf-{version}",
        urls = ["https://github.com/protocolbuffers/protobuf/releases/download/v{version}/protobuf-all-{version}.tar.gz"],
        use_category = ["dataplane", "controlplane"],
        cpe = "N/A",
    ),
    grpc_httpjson_transcoding = dict(
        project_name = "grpc-httpjson-transcoding",
        project_url = "https://github.com/grpc-ecosystem/grpc-httpjson-transcoding",
        # 2020-03-02
        version = "faf8af1e9788cd4385b94c8f85edab5ea5d4b2d6",
        sha256 = "62c8cb5ea2cca1142cde9d4a0778c52c6022345c3268c60ef81666946b958ad5",
        strip_prefix = "grpc-httpjson-transcoding-{version}",
        urls = ["https://github.com/grpc-ecosystem/grpc-httpjson-transcoding/archive/{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    io_bazel_rules_go = dict(
        project_name = "Go rules for Bazel",
        project_url = "https://github.com/bazelbuild/rules_go",
        version = "0.23.7",
        sha256 = "0310e837aed522875791750de44408ec91046c630374990edd51827cb169f616",
        urls = ["https://github.com/bazelbuild/rules_go/releases/download/v{version}/rules_go-v{version}.tar.gz"],
        use_category = ["build"],
    ),
    rules_cc = dict(
        project_name = "C++ rules for Bazel",
        project_url = "https://github.com/bazelbuild/rules_cc",
        # 2020-05-13
        # TODO(lizan): pin to a point releases when there's a released version.
        version = "818289e5613731ae410efb54218a4077fb9dbb03",
        sha256 = "9d48151ea71b3e225adfb6867e6d2c7d0dce46cbdc8710d9a9a628574dfd40a0",
        strip_prefix = "rules_cc-{version}",
        urls = ["https://github.com/bazelbuild/rules_cc/archive/{version}.tar.gz"],
        use_category = ["build"],
    ),
    rules_foreign_cc = dict(
        project_name = "Rules for using foreign build systems in Bazel",
        project_url = "https://github.com/bazelbuild/rules_foreign_cc",
        # 2020-08-21
        version = "594bf4d7731e606a705f3ad787dd0a70c5a28b30",
        sha256 = "2b1cf88de0b6e0195f6571cfde3a5bd406d11b42117d6adef2395c9525a1902e",
        strip_prefix = "rules_foreign_cc-{version}",
        urls = ["https://github.com/bazelbuild/rules_foreign_cc/archive/{version}.tar.gz"],
        use_category = ["build"],
    ),
    rules_python = dict(
        project_name = "Python rules for Bazel",
        project_url = "https://github.com/bazelbuild/rules_python",
        # 2020-04-09
        # TODO(htuch): revert back to a point releases when pip3_import appears.
        version = "a0fbf98d4e3a232144df4d0d80b577c7a693b570",
        sha256 = "76a8fd4e7eca2a3590f816958faa0d83c9b2ce9c32634c5c375bcccf161d3bb5",
        strip_prefix = "rules_python-{version}",
        urls = ["https://github.com/bazelbuild/rules_python/archive/{version}.tar.gz"],
        use_category = ["build"],
    ),
    six = dict(
        project_name = "Six",
        project_url = "https://pypi.org/project/six",
        version = "1.12.0",
        sha256 = "d16a0141ec1a18405cd4ce8b4613101da75da0e9a7aec5bdd4fa804d0e0eba73",
        urls = ["https://files.pythonhosted.org/packages/dd/bf/4138e7bfb757de47d1f4b6994648ec67a51efe58fa907c1e11e350cddfca/six-{version}.tar.gz"],
        use_category = ["other"],
    ),
    io_opencensus_cpp = dict(
        project_name = "OpenCensus C++",
        project_url = "https://pypi.org/project/six/",
        # 2020-06-01
        version = "7877337633466358ed680f9b26967da5b310d7aa",
        sha256 = "12ff300fa804f97bd07e2ff071d969e09d5f3d7bbffeac438c725fa52a51a212",
        strip_prefix = "opencensus-cpp-{version}",
        urls = ["https://github.com/census-instrumentation/opencensus-cpp/archive/{version}.tar.gz"],
        use_category = ["observability"],
        cpe = "N/A",
    ),
    com_github_curl = dict(
        project_name = "curl",
        project_url = "https://curl.haxx.se",
        version = "7.69.1",
        sha256 = "01ae0c123dee45b01bbaef94c0bc00ed2aec89cb2ee0fd598e0d302a6b5e0a98",
        strip_prefix = "curl-{version}",
        urls = ["https://github.com/curl/curl/releases/download/curl-7_69_1/curl-{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_googlesource_chromium_v8 = dict(
        project_name = "V8",
        project_url = "https://v8.dev",
        version = "8.3",
        # This archive was created using https://storage.googleapis.com/envoyproxy-wee8/wee8-archive.sh
        # and contains complete checkout of V8 with all dependencies necessary to build wee8.
        sha256 = "cc6f5357cd10922bfcf667bd882624ad313e21b009b919ce00f322f390012476",
        urls = ["https://storage.googleapis.com/envoyproxy-wee8/wee8-{version}.110.9.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_googlesource_quiche = dict(
        project_name = "QUICHE",
        project_url = "https://quiche.googlesource.com/quiche",
        # Static snapshot of https://quiche.googlesource.com/quiche/+archive/96bd860bec207d4b722ab7f319fa47be129a85cd.tar.gz
        version = "96bd860bec207d4b722ab7f319fa47be129a85cd",
        sha256 = "d7129a2f41f2bd00a8a38b33f9b7b955d3e7de3dec20f69b70d7000d3a856360",
        urls = ["https://storage.googleapis.com/quiche-envoy-integration/{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_googlesource_googleurl = dict(
        project_name = "Chrome URL parsing library",
        project_url = "https://quiche.googlesource.com/googleurl",
        # Static snapshot of https://quiche.googlesource.com/quiche/+archive/ef0d23689e240e6c8de4c3a5296b209128c87373.tar.gz.
        # 2020-08-05
        version = "ef0d23689e240e6c8de4c3a5296b209128c87373",
        sha256 = "d769283fed1319bca68bae8bdd47fbc3a7933999329eee850eff1f1ea61ce176",
        urls = ["https://storage.googleapis.com/quiche-envoy-integration/googleurl_{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_google_cel_cpp = dict(
        project_name = "Common Expression Language C++",
        project_url = "https://opensource.google/projects/cel",
        # 2020-07-14
        version = "b9453a09b28a1531c4917e8792b3ea61f6b1a447",
        sha256 = "cad7d01139947d78e413d112cb8f7431fbb33cf66b0adf9c280824803fc2a72e",
        strip_prefix = "cel-cpp-{version}",
        urls = ["https://github.com/google/cel-cpp/archive/{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_github_google_flatbuffers = dict(
        project_name = "FlatBuffers",
        project_url = "https://github.com/google/flatbuffers",
        version = "a83caf5910644ba1c421c002ef68e42f21c15f9f",
        sha256 = "b8efbc25721e76780752bad775a97c3f77a0250271e2db37fc747b20e8b0f24a",
        strip_prefix = "flatbuffers-{version}",
        urls = ["https://github.com/google/flatbuffers/archive/{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_googlesource_code_re2 = dict(
        project_name = "RE2",
        project_url = "https://github.com/google/re2",
        # 2020-07-06
        version = "2020-07-06",
        sha256 = "2e9489a31ae007c81e90e8ec8a15d62d58a9c18d4fd1603f6441ef248556b41f",
        strip_prefix = "re2-{version}",
        urls = ["https://github.com/google/re2/archive/{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    # Included to access FuzzedDataProvider.h. This is compiler agnostic but
    # provided as part of the compiler-rt source distribution. We can't use the
    # Clang variant as we are not a Clang-LLVM only shop today.
    org_llvm_releases_compiler_rt = dict(
        project_name = "compiler-rt",
        project_url = "https://compiler-rt.llvm.org",
        version = "10.0.0",
        sha256 = "6a7da64d3a0a7320577b68b9ca4933bdcab676e898b759850e827333c3282c75",
        # Only allow peeking at fuzzer related files for now.
        strip_prefix = "compiler-rt-{version}.src",
        urls = ["https://github.com/llvm/llvm-project/releases/download/llvmorg-{version}/compiler-rt-{version}.src.tar.xz"],
        use_category = ["test"],
    ),
    upb = dict(
        project_name = "upb",
        project_url = "https://github.com/protocolbuffers/upb",
        # 2019-11-19
        version = "8a3ae1ef3e3e3f26b45dec735c5776737fc7247f",
        sha256 = "e9f281c56ab1eb1f97a80ca8a83bb7ef73d230eabb8591f83876f4e7b85d9b47",
        strip_prefix = "upb-{version}",
        urls = ["https://github.com/protocolbuffers/upb/archive/{version}.tar.gz"],
        use_category = ["dataplane", "controlplane"],
        cpe = "N/A",
    ),
    kafka_source = dict(
        project_name = "Kafka (source)",
        project_url = "https://kafka.apache.org",
        version = "2.4.1",
        sha256 = "740236f44d66e33ea83382383b4fb7eabdab7093a644b525dd5ec90207f933bd",
        strip_prefix = "kafka-{version}/clients/src/main/resources/common/message",
        urls = ["https://github.com/apache/kafka/archive/{version}.zip"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:apache:kafka:*",
    ),
    kafka_server_binary = dict(
        project_name = "Kafka (server binary)",
        project_url = "https://kafka.apache.org",
        version = "2.4.1",
        sha256 = "2177cbd14118999e1d76fec628ca78ace7e6f841219dbc6035027c796bbe1a2a",
        strip_prefix = "kafka_2.12-{version}",
        urls = ["http://us.mirrors.quenda.co/apache/kafka/{version}/kafka_2.12-{version}.tgz"],
        use_category = ["test"],
    ),
    kafka_python_client = dict(
        project_name = "Kafka (Python client)",
        project_url = "https://kafka.apache.org",
        version = "2.0.1",
        sha256 = "05f7c6eecb402f11fcb7e524c903f1ba1c38d3bdc9bf42bc8ec3cf7567b9f979",
        strip_prefix = "kafka-python-{version}",
        urls = ["https://github.com/dpkp/kafka-python/archive/{version}.tar.gz"],
        use_category = ["test"],
    ),
    org_unicode_icuuc = dict(
        project_name = "International Components for Unicode",
        project_url = "https://github.com/unicode-org/icu",
        version = "67-1",
        strip_prefix = "icu",
        sha256 = "94a80cd6f251a53bd2a997f6f1b5ac6653fe791dfab66e1eb0227740fb86d5dc",
        urls = ["https://github.com/unicode-org/icu/releases/download/release-{version}/icu4c-67_1-src.tgz"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:icu-project:international_components_for_unicode",
    ),
    proxy_wasm_cpp_sdk = dict(
        project_name = "WebAssembly for Proxies (C++ SDK)",
        project_url = "https://github.com/proxy-wasm/proxy-wasm-cpp-sdk",
        version = "5cec30b448975e1fd3f4117311f0957309df5cb0",
        sha256 = "7d9e1f2e299215ed3e5fa8c8149740872b1100cfe3230fc639f967d9dcfd812e",
        strip_prefix = "proxy-wasm-cpp-sdk-{version}",
        urls = ["https://github.com/proxy-wasm/proxy-wasm-cpp-sdk/archive/{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    proxy_wasm_cpp_host = dict(
        project_name = "WebAssembly for Proxies (C++ host implementation)",
        project_url = "https://github.com/proxy-wasm/proxy-wasm-cpp-host",
        version = "928db4d79ec7b90aea3ad13ea5df36dc60c9c31d",
        sha256 = "494d3f81156b92bac640c26000497fbf3a7b1bc35f9789594280450c6e5d8129",
        strip_prefix = "proxy-wasm-cpp-host-{version}",
        urls = ["https://github.com/proxy-wasm/proxy-wasm-cpp-host/archive/{version}.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    emscripten_toolchain = dict(
        project_name = "Emscripten SDK",
        project_url = "https://github.com/emscripten-core/emsdk",
        version = "dec8a63594753fe5f4ad3b47850bf64d66c14a4e",
        sha256 = "2bdbee6947e32ad1e03cd075b48fda493ab16157b2b0225b445222cd528e1843",
        patch_cmds = [
            "./emsdk install 1.39.19-upstream",
            "./emsdk activate --embedded 1.39.19-upstream",
        ],
        strip_prefix = "emsdk-{version}",
        urls = ["https://github.com/emscripten-core/emsdk/archive/{version}.tar.gz"],
        use_category = ["build"],
    ),
    rules_antlr = dict(
        project_name = "ANTLR Rules for Bazel",
        project_url = "https://github.com/marcohu/rules_antlr",
        version = "3cc2f9502a54ceb7b79b37383316b23c4da66f9a",
        sha256 = "7249d1569293d9b239e23c65f6b4c81a07da921738bde0dfeb231ed98be40429",
        strip_prefix = "rules_antlr-{version}",
        urls = ["https://github.com/marcohu/rules_antlr/archive/{version}.tar.gz"],
        use_category = ["build"],
    ),
    antlr4_runtimes = dict(
        project_name = "ANTLR v4",
        project_url = "https://github.com/antlr/antlr4",
        version = "4.7.1",
        sha256 = "4d0714f441333a63e50031c9e8e4890c78f3d21e053d46416949803e122a6574",
        strip_prefix = "antlr4-{version}",
        urls = ["https://github.com/antlr/antlr4/archive/{version}.tar.gz"],
        use_category = ["build"],
    ),
)

# Interpolate {version} in the above dependency specs. This code should be capable of running in both Python
# and Starlark.
def _dependency_repositories():
    locations = {}
    for key, location in DEPENDENCY_REPOSITORIES_SPEC.items():
        mutable_location = dict(location)
        locations[key] = mutable_location

        # Fixup with version information.
        if "version" in location:
            if "strip_prefix" in location:
                mutable_location["strip_prefix"] = location["strip_prefix"].format(version = location["version"])
            mutable_location["urls"] = [url.format(version = location["version"]) for url in location["urls"]]
    return locations

DEPENDENCY_REPOSITORIES = _dependency_repositories()

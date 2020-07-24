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
    # This dependency is used in build process.
    "build",
    # This dependency is used for unit tests.
    "test",
    # This dependency is used in API protos.
    "api",
    # This dependency is used in processing downstream or upstream requests.
    "dataplane",
    # This dependency is used to process xDS requests.
    "controlplane",
    # This dependecy is used for logging, metrics or tracing. It may process unstrusted input.
    "observability",
    # This dependency does not handle untrusted data and is used for various utility purposes.
    "other",
]

# Components with these use categories are not required to specify the 'cpe' annotation.
USE_CATEGORIES_WITH_CPE_OPTIONAL = ["build", "test", "other"]

DEPENDENCY_REPOSITORIES = dict(
    bazel_compdb = dict(
        sha256 = "87e376a685eacfb27bcc0d0cdf5ded1d0b99d868390ac50f452ba6ed781caffe",
        strip_prefix = "bazel-compilation-database-0.4.2",
        urls = ["https://github.com/grailbio/bazel-compilation-database/archive/0.4.2.tar.gz"],
        use_category = ["build"],
    ),
    bazel_gazelle = dict(
        sha256 = "86c6d481b3f7aedc1d60c1c211c6f76da282ae197c3b3160f54bd3a8f847896f",
        urls = ["https://github.com/bazelbuild/bazel-gazelle/releases/download/v0.19.1/bazel-gazelle-v0.19.1.tar.gz"],
        use_category = ["build"],
    ),
    bazel_toolchains = dict(
        sha256 = "882fecfc88d3dc528f5c5681d95d730e213e39099abff2e637688a91a9619395",
        strip_prefix = "bazel-toolchains-3.4.0",
        urls = [
            "https://github.com/bazelbuild/bazel-toolchains/releases/download/3.4.0/bazel-toolchains-3.4.0.tar.gz",
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/archive/3.4.0.tar.gz",
        ],
        use_category = ["build"],
    ),
    build_bazel_rules_apple = dict(
        sha256 = "7a7afdd4869bb201c9352eed2daf37294d42b093579b70423490c1b4d4f6ce42",
        urls = ["https://github.com/bazelbuild/rules_apple/releases/download/0.19.0/rules_apple.0.19.0.tar.gz"],
        use_category = ["build"],
    ),
    envoy_build_tools = dict(
        sha256 = "88e58fdb42021e64a0b35ae3554a82e92f5c37f630a4dab08a132fc77f8db4b7",
        strip_prefix = "envoy-build-tools-1d6573e60207efaae6436b25ecc594360294f63a",
        # 2020-07-18
        urls = ["https://github.com/envoyproxy/envoy-build-tools/archive/1d6573e60207efaae6436b25ecc594360294f63a.tar.gz"],
        use_category = ["build"],
    ),
    boringssl = dict(
        sha256 = "07f1524766b9ed1543674b48e7fce7e3569b6e2b6c0c43ec124dedee9b60f641",
        strip_prefix = "boringssl-a0899df79b3a63e606448c72d63a090d86bdb75b",
        # To update BoringSSL, which tracks Chromium releases:
        # 1. Open https://omahaproxy.appspot.com/ and note <current_version> of linux/stable release.
        # 2. Open https://chromium.googlesource.com/chromium/src/+/refs/tags/<current_version>/DEPS and note <boringssl_revision>.
        # 3. Find a commit in BoringSSL's "master-with-bazel" branch that merges <boringssl_revision>.
        #
        # chromium-84.0.4147.45(beta)
        # 2020-05-14
        urls = ["https://github.com/google/boringssl/archive/a0899df79b3a63e606448c72d63a090d86bdb75b.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    boringssl_fips = dict(
        sha256 = "3b5fdf23274d4179c2077b5e8fa625d9debd7a390aac1d165b7e47234f648bb8",
        # fips-20190808
        urls = ["https://commondatastorage.googleapis.com/chromium-boringssl-fips/boringssl-ae223d6138807a13006342edfeef32e813246b39.tar.xz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_google_absl = dict(
        sha256 = "ec8ef47335310cc3382bdc0d0cc1097a001e67dc83fcba807845aa5696e7e1e4",
        strip_prefix = "abseil-cpp-302b250e1d917ede77b5ff00a6fd9f28430f1563",
        # 2020-07-13
        urls = ["https://github.com/abseil/abseil-cpp/archive/302b250e1d917ede77b5ff00a6fd9f28430f1563.tar.gz"],
        use_category = ["dataplane", "controlplane"],
        cpe = "N/A",
    ),
    com_github_apache_thrift = dict(
        sha256 = "7d59ac4fdcb2c58037ebd4a9da5f9a49e3e034bf75b3f26d9fe48ba3d8806e6b",
        strip_prefix = "thrift-0.11.0",
        urls = ["https://files.pythonhosted.org/packages/c6/b4/510617906f8e0c5660e7d96fbc5585113f83ad547a3989b80297ac72a74c/thrift-0.11.0.tar.gz"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:apache:thrift:*",
    ),
    com_github_c_ares_c_ares = dict(
        sha256 = "d08312d0ecc3bd48eee0a4cc0d2137c9f194e0a28de2028928c0f6cae85f86ce",
        strip_prefix = "c-ares-1.16.1",
        urls = ["https://github.com/c-ares/c-ares/releases/download/cares-1_16_1/c-ares-1.16.1.tar.gz"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:c-ares_project:c-ares:*",
    ),
    com_github_circonus_labs_libcircllhist = dict(
        sha256 = "8165aa25e529d7d4b9ae849d3bf30371255a99d6db0421516abcff23214cdc2c",
        strip_prefix = "libcircllhist-63a16dd6f2fc7bc841bb17ff92be8318df60e2e1",
        # 2019-02-11
        urls = ["https://github.com/circonus-labs/libcircllhist/archive/63a16dd6f2fc7bc841bb17ff92be8318df60e2e1.tar.gz"],
        use_category = ["observability"],
        cpe = "N/A",
    ),
    com_github_cyan4973_xxhash = dict(
        sha256 = "952ebbf5b11fbf59ae5d760a562d1e9112278f244340ad7714e8556cbe54f7f7",
        strip_prefix = "xxHash-0.7.3",
        urls = ["https://github.com/Cyan4973/xxHash/archive/v0.7.3.tar.gz"],
        use_category = ["dataplane", "controlplane"],
        cpe = "N/A",
    ),
    com_github_envoyproxy_sqlparser = dict(
        sha256 = "96c10c8e950a141a32034f19b19cdeb1da48fe859cf96ae5e19f894f36c62c71",
        strip_prefix = "sql-parser-3b40ba2d106587bdf053a292f7e3bb17e818a57f",
        # 2020-06-10
        urls = ["https://github.com/envoyproxy/sql-parser/archive/3b40ba2d106587bdf053a292f7e3bb17e818a57f.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_github_mirror_tclap = dict(
        sha256 = "f0ede0721dddbb5eba3a47385a6e8681b14f155e1129dd39d1a959411935098f",
        strip_prefix = "tclap-tclap-1-2-1-release-final",
        urls = ["https://github.com/mirror/tclap/archive/tclap-1-2-1-release-final.tar.gz"],
        use_category = ["other"],
    ),
    com_github_fmtlib_fmt = dict(
        sha256 = "5014aacf55285bf79654539791de0d6925063fddf4dfdd597ef76b53eb994f86",
        strip_prefix = "fmt-e2ff910675c7800e5c4e28e1509ca6a50bdceafa",
        # 2020-04-29
        urls = ["https://github.com/fmtlib/fmt/archive/e2ff910675c7800e5c4e28e1509ca6a50bdceafa.tar.gz"],
        use_category = ["observability"],
        cpe = "N/A",
    ),
    com_github_gabime_spdlog = dict(
        sha256 = "378a040d91f787aec96d269b0c39189f58a6b852e4cbf9150ccfacbe85ebbbfc",
        strip_prefix = "spdlog-1.6.1",
        urls = ["https://github.com/gabime/spdlog/archive/v1.6.1.tar.gz"],
        use_category = ["observability"],
        cpe = "N/A",
    ),
    com_github_google_libprotobuf_mutator = dict(
        sha256 = "d51365191580c4bf5e9ff104eebcfe34f7ff5f471006d7a460c15dcb3657501c",
        strip_prefix = "libprotobuf-mutator-7a2ed51a6b682a83e345ff49fc4cfd7ca47550db",
        # 2020-06-25
        urls = ["https://github.com/google/libprotobuf-mutator/archive/7a2ed51a6b682a83e345ff49fc4cfd7ca47550db.tar.gz"],
        use_category = ["test"],
    ),
    com_github_gperftools_gperftools = dict(
        sha256 = "240deacdd628b6459671b83eb0c4db8e97baadf659f25b92e9a078d536bd513e",
        strip_prefix = "gperftools-2.8",
        urls = ["https://github.com/gperftools/gperftools/releases/download/gperftools-2.8/gperftools-2.8.tar.gz"],
        use_category = ["test"],
    ),
    com_github_grpc_grpc = dict(
        # TODO(JimmyCYJ): Bump to release 1.27
        # This sha on grpc:v1.25.x branch is specifically chosen to fix gRPC STS call credential options.
        sha256 = "bbc8f020f4e85ec029b047fab939b8c81f3d67254b5c724e1003a2bc49ddd123",
        strip_prefix = "grpc-d8f4928fa779f6005a7fe55a176bdb373b0f910f",
        # 2020-02-11
        urls = ["https://github.com/grpc/grpc/archive/d8f4928fa779f6005a7fe55a176bdb373b0f910f.tar.gz"],
        use_category = ["dataplane", "controlplane"],
        cpe = "cpe:2.3:a:grpc:grpc:*",
    ),
    com_github_luajit_luajit = dict(
        sha256 = "409f7fe570d3c16558e594421c47bdd130238323c9d6fd6c83dedd2aaeb082a8",
        strip_prefix = "LuaJIT-2.1.0-beta3",
        urls = ["https://github.com/LuaJIT/LuaJIT/archive/v2.1.0-beta3.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_github_moonjit_moonjit = dict(
        sha256 = "83deb2c880488dfe7dd8ebf09e3b1e7613ef4b8420de53de6f712f01aabca2b6",
        strip_prefix = "moonjit-2.2.0",
        urls = ["https://github.com/moonjit/moonjit/archive/2.2.0.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_github_nghttp2_nghttp2 = dict(
        sha256 = "eacc6f0f8543583ecd659faf0a3f906ed03826f1d4157b536b4b385fe47c5bb8",
        strip_prefix = "nghttp2-1.41.0",
        urls = ["https://github.com/nghttp2/nghttp2/releases/download/v1.41.0/nghttp2-1.41.0.tar.gz"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:nghttp2:nghttp2:*",
    ),
    io_opentracing_cpp = dict(
        sha256 = "015c4187f7a6426a2b5196f0ccd982aa87f010cf61f507ae3ce5c90523f92301",
        strip_prefix = "opentracing-cpp-1.5.1",
        urls = ["https://github.com/opentracing/opentracing-cpp/archive/v1.5.1.tar.gz"],
        use_category = ["observability"],
        cpe = "N/A",
    ),
    com_lightstep_tracer_cpp = dict(
        sha256 = "0e99716598c010e56bc427ea3482be5ad2c534be8b039d172564deec1264a213",
        strip_prefix = "lightstep-tracer-cpp-3efe2372ee3d7c2138d6b26e542d757494a7938d",
        # 2020-03-24
        urls = ["https://github.com/lightstep/lightstep-tracer-cpp/archive/3efe2372ee3d7c2138d6b26e542d757494a7938d.tar.gz"],
        use_category = ["observability"],
        cpe = "N/A",
    ),
    com_github_datadog_dd_opentracing_cpp = dict(
        sha256 = "b84fd2fb0bb0578af4901db31d1c0ae909b532a1016fe6534cbe31a6c3ad6924",
        strip_prefix = "dd-opentracing-cpp-1.1.5",
        urls = ["https://github.com/DataDog/dd-opentracing-cpp/archive/v1.1.5.tar.gz"],
        use_category = ["observability"],
        cpe = "N/A",
    ),
    com_github_google_benchmark = dict(
        sha256 = "23082937d1663a53b90cb5b61df4bcc312f6dee7018da78ba00dd6bd669dfef2",
        strip_prefix = "benchmark-1.5.1",
        urls = ["https://github.com/google/benchmark/archive/v1.5.1.tar.gz"],
        use_category = ["test"],
    ),
    com_github_libevent_libevent = dict(
        sha256 = "c64156c24602ab7a5c66937d774cc55868911d5bbbf1650792f5877744b1c2d9",
        # This SHA includes the new "prepare" and "check" watchers, used for event loop performance
        # stats (see https://github.com/libevent/libevent/pull/793) and the fix for a race condition
        # in the watchers (see https://github.com/libevent/libevent/pull/802).
        # This also includes the fixes for https://github.com/libevent/libevent/issues/806
        # and https://github.com/lyft/envoy-mobile/issues/215.
        # This also include the fixes for Phantom events with EV_ET (see
        # https://github.com/libevent/libevent/issues/984).
        # TODO(adip): Update to v2.2 when it is released.
        strip_prefix = "libevent-06a11929511bebaaf40c52aaf91de397b1782ba2",
        # 2020-05-08
        urls = ["https://github.com/libevent/libevent/archive/06a11929511bebaaf40c52aaf91de397b1782ba2.tar.gz"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:libevent_project:libevent:*",
    ),
    net_zlib = dict(
        # Use the dev branch of zlib to resolve fuzz bugs and out of bound
        # errors resulting in crashes in zlib 1.2.11.
        # TODO(asraa): Remove when zlib > 1.2.11 is released.
        sha256 = "155a8f8c1a753fb05b16a1b0cc0a0a9f61a78e245f9e0da483d13043b3bcbf2e",
        strip_prefix = "zlib-79baebe50e4d6b73ae1f8b603f0ef41300110aa3",
        # 2019-04-14 development branch
        urls = ["https://github.com/madler/zlib/archive/79baebe50e4d6b73ae1f8b603f0ef41300110aa3.tar.gz"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:gnu:zlib:*",
    ),
    com_github_jbeder_yaml_cpp = dict(
        sha256 = "17ffa6320c33de65beec33921c9334dee65751c8a4b797ba5517e844062b98f1",
        strip_prefix = "yaml-cpp-6701275f1910bf63631528dfd9df9c3ac787365b",
        # 2020-05-25
        urls = ["https://github.com/jbeder/yaml-cpp/archive/6701275f1910bf63631528dfd9df9c3ac787365b.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_github_msgpack_msgpack_c = dict(
        sha256 = "433cbcd741e1813db9ae4b2e192b83ac7b1d2dd7968a3e11470eacc6f4ab58d2",
        strip_prefix = "msgpack-3.2.1",
        urls = ["https://github.com/msgpack/msgpack-c/releases/download/cpp-3.2.1/msgpack-3.2.1.tar.gz"],
        use_category = ["observability"],
        cpe = "N/A",
    ),
    com_github_google_jwt_verify = dict(
        sha256 = "f1fde4f3ebb3b2d841332c7a02a4b50e0529a19709934c63bc6208d1bbe28fb1",
        strip_prefix = "jwt_verify_lib-7276a339af8426724b744216f619c99152f8c141",
        # 2020-07-09
        urls = ["https://github.com/google/jwt_verify_lib/archive/7276a339af8426724b744216f619c99152f8c141.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_github_nodejs_http_parser = dict(
        sha256 = "8fa0ab8770fd8425a9b431fdbf91623c4d7a9cdb842b9339289bd2b0b01b0d3d",
        strip_prefix = "http-parser-2.9.3",
        urls = ["https://github.com/nodejs/http-parser/archive/v2.9.3.tar.gz"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:nodejs:node.js:*",
    ),
    com_github_pallets_jinja = dict(
        sha256 = "db49236731373e4f3118af880eb91bb0aa6978bc0cf8b35760f6a026f1a9ffc4",
        strip_prefix = "jinja-2.10.3",
        urls = ["https://github.com/pallets/jinja/archive/2.10.3.tar.gz"],
        use_category = ["build"],
    ),
    com_github_pallets_markupsafe = dict(
        sha256 = "222a10e3237d92a9cd45ed5ea882626bc72bc5e0264d3ed0f2c9129fa69fc167",
        strip_prefix = "markupsafe-1.1.1/src",
        urls = ["https://github.com/pallets/markupsafe/archive/1.1.1.tar.gz"],
        use_category = ["build"],
    ),
    com_github_tencent_rapidjson = dict(
        sha256 = "a2faafbc402394df0fa94602df4b5e4befd734aad6bb55dfef46f62fcaf1090b",
        strip_prefix = "rapidjson-dfbe1db9da455552f7a9ad5d2aea17dd9d832ac1",
        # Changes through 2019-12-02
        urls = ["https://github.com/Tencent/rapidjson/archive/dfbe1db9da455552f7a9ad5d2aea17dd9d832ac1.tar.gz"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:tencent:rapidjson:*",
    ),
    com_github_twitter_common_lang = dict(
        sha256 = "56d1d266fd4767941d11c27061a57bc1266a3342e551bde3780f9e9eb5ad0ed1",
        strip_prefix = "twitter.common.lang-0.3.9/src",
        urls = ["https://files.pythonhosted.org/packages/08/bc/d6409a813a9dccd4920a6262eb6e5889e90381453a5f58938ba4cf1d9420/twitter.common.lang-0.3.9.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_github_twitter_common_rpc = dict(
        sha256 = "0792b63fb2fb32d970c2e9a409d3d00633190a22eb185145fe3d9067fdaa4514",
        strip_prefix = "twitter.common.rpc-0.3.9/src",
        urls = ["https://files.pythonhosted.org/packages/be/97/f5f701b703d0f25fbf148992cd58d55b4d08d3db785aad209255ee67e2d0/twitter.common.rpc-0.3.9.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_github_twitter_common_finagle_thrift = dict(
        sha256 = "1e3a57d11f94f58745e6b83348ecd4fa74194618704f45444a15bc391fde497a",
        strip_prefix = "twitter.common.finagle-thrift-0.3.9/src",
        urls = ["https://files.pythonhosted.org/packages/f9/e7/4f80d582578f8489226370762d2cf6bc9381175d1929eba1754e03f70708/twitter.common.finagle-thrift-0.3.9.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_google_googletest = dict(
        sha256 = "9dc9157a9a1551ec7a7e43daea9a694a0bb5fb8bec81235d8a1e6ef64c716dcb",
        strip_prefix = "googletest-release-1.10.0",
        urls = ["https://github.com/google/googletest/archive/release-1.10.0.tar.gz"],
        use_category = ["test"],
    ),
    com_google_protobuf = dict(
        sha256 = "d7cfd31620a352b2ee8c1ed883222a0d77e44346643458e062e86b1d069ace3e",
        strip_prefix = "protobuf-3.10.1",
        urls = ["https://github.com/protocolbuffers/protobuf/releases/download/v3.10.1/protobuf-all-3.10.1.tar.gz"],
        use_category = ["dataplane", "controlplane"],
        cpe = "N/A",
    ),
    grpc_httpjson_transcoding = dict(
        sha256 = "62c8cb5ea2cca1142cde9d4a0778c52c6022345c3268c60ef81666946b958ad5",
        strip_prefix = "grpc-httpjson-transcoding-faf8af1e9788cd4385b94c8f85edab5ea5d4b2d6",
        # 2020-03-02
        urls = ["https://github.com/grpc-ecosystem/grpc-httpjson-transcoding/archive/faf8af1e9788cd4385b94c8f85edab5ea5d4b2d6.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    io_bazel_rules_go = dict(
        sha256 = "a8d6b1b354d371a646d2f7927319974e0f9e52f73a2452d2b3877118169eb6bb",
        urls = ["https://github.com/bazelbuild/rules_go/releases/download/v0.23.3/rules_go-v0.23.3.tar.gz"],
        use_category = ["build"],
    ),
    rules_cc = dict(
        sha256 = "9d48151ea71b3e225adfb6867e6d2c7d0dce46cbdc8710d9a9a628574dfd40a0",
        strip_prefix = "rules_cc-818289e5613731ae410efb54218a4077fb9dbb03",
        # 2020-05-13
        # TODO(lizan): pin to a point releases when there's a released version.
        urls = ["https://github.com/bazelbuild/rules_cc/archive/818289e5613731ae410efb54218a4077fb9dbb03.tar.gz"],
        use_category = ["build"],
    ),
    rules_foreign_cc = dict(
        sha256 = "7ca49ac5b0bc8f5a2c9a7e87b7f86aca604bda197259c9b96f8b7f0a4f38b57b",
        strip_prefix = "rules_foreign_cc-f54b7ae56dcf1b81bcafed3a08d58fc08ac095a7",
        # 2020-06-09
        urls = ["https://github.com/bazelbuild/rules_foreign_cc/archive/f54b7ae56dcf1b81bcafed3a08d58fc08ac095a7.tar.gz"],
        use_category = ["build"],
    ),
    rules_python = dict(
        sha256 = "76a8fd4e7eca2a3590f816958faa0d83c9b2ce9c32634c5c375bcccf161d3bb5",
        strip_prefix = "rules_python-a0fbf98d4e3a232144df4d0d80b577c7a693b570",
        # 2020-04-09
        # TODO(htuch): revert back to a point releases when pip3_import appears.
        urls = ["https://github.com/bazelbuild/rules_python/archive/a0fbf98d4e3a232144df4d0d80b577c7a693b570.tar.gz"],
        use_category = ["build"],
    ),
    six = dict(
        sha256 = "d16a0141ec1a18405cd4ce8b4613101da75da0e9a7aec5bdd4fa804d0e0eba73",
        urls = ["https://files.pythonhosted.org/packages/dd/bf/4138e7bfb757de47d1f4b6994648ec67a51efe58fa907c1e11e350cddfca/six-1.12.0.tar.gz"],
        use_category = ["other"],
    ),
    io_opencensus_cpp = dict(
        sha256 = "12ff300fa804f97bd07e2ff071d969e09d5f3d7bbffeac438c725fa52a51a212",
        strip_prefix = "opencensus-cpp-7877337633466358ed680f9b26967da5b310d7aa",
        # 2020-06-01
        urls = ["https://github.com/census-instrumentation/opencensus-cpp/archive/7877337633466358ed680f9b26967da5b310d7aa.tar.gz"],
        use_category = ["observability"],
        cpe = "N/A",
    ),
    com_github_curl = dict(
        sha256 = "01ae0c123dee45b01bbaef94c0bc00ed2aec89cb2ee0fd598e0d302a6b5e0a98",
        strip_prefix = "curl-7.69.1",
        urls = ["https://github.com/curl/curl/releases/download/curl-7_69_1/curl-7.69.1.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_googlesource_chromium_v8 = dict(
        # This archive was created using https://storage.googleapis.com/envoyproxy-wee8/wee8-archive.sh
        # and contains complete checkout of V8 with all dependencies necessary to build wee8.
        sha256 = "cc6f5357cd10922bfcf667bd882624ad313e21b009b919ce00f322f390012476",
        urls = ["https://storage.googleapis.com/envoyproxy-wee8/wee8-8.3.110.9.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_googlesource_quiche = dict(
        # Static snapshot of https://quiche.googlesource.com/quiche/+archive/b2b8ff25f5a565324b93411ca29c3403ccbca969.tar.gz
        sha256 = "792924bbf27203bb0d1d08c99597a30793ef8f4cfa2df99792aea7200f1b27e3",
        urls = ["https://storage.googleapis.com/quiche-envoy-integration/b2b8ff25f5a565324b93411ca29c3403ccbca969.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_googlesource_googleurl = dict(
        # Static snapshot of https://quiche.googlesource.com/quiche/+archive/googleurl_6dafefa72cba2ab2ba4922d17a30618e9617c7cf.tar.gz
        sha256 = "f1ab73ddd1a7db4e08a9e4db6c2e98e5a0a7bbaca08f5fee0d73adb02c24e44a",
        urls = ["https://storage.googleapis.com/quiche-envoy-integration/googleurl_6dafefa72cba2ab2ba4922d17a30618e9617c7cf.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_google_cel_cpp = dict(
        sha256 = "cad7d01139947d78e413d112cb8f7431fbb33cf66b0adf9c280824803fc2a72e",
        strip_prefix = "cel-cpp-b9453a09b28a1531c4917e8792b3ea61f6b1a447",
        # 2020-07-14
        urls = ["https://github.com/google/cel-cpp/archive/b9453a09b28a1531c4917e8792b3ea61f6b1a447.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    com_googlesource_code_re2 = dict(
        sha256 = "2e9489a31ae007c81e90e8ec8a15d62d58a9c18d4fd1603f6441ef248556b41f",
        strip_prefix = "re2-2020-07-06",
        # 2020-07-06
        urls = ["https://github.com/google/re2/archive/2020-07-06.tar.gz"],
        use_category = ["dataplane"],
        cpe = "N/A",
    ),
    # Included to access FuzzedDataProvider.h. This is compiler agnostic but
    # provided as part of the compiler-rt source distribution. We can't use the
    # Clang variant as we are not a Clang-LLVM only shop today.
    org_llvm_releases_compiler_rt = dict(
        sha256 = "6a7da64d3a0a7320577b68b9ca4933bdcab676e898b759850e827333c3282c75",
        # Only allow peeking at fuzzer related files for now.
        strip_prefix = "compiler-rt-10.0.0.src",
        urls = ["https://github.com/llvm/llvm-project/releases/download/llvmorg-10.0.0/compiler-rt-10.0.0.src.tar.xz"],
        use_category = ["test"],
    ),
    upb = dict(
        sha256 = "e9f281c56ab1eb1f97a80ca8a83bb7ef73d230eabb8591f83876f4e7b85d9b47",
        strip_prefix = "upb-8a3ae1ef3e3e3f26b45dec735c5776737fc7247f",
        # 2019-11-19
        urls = ["https://github.com/protocolbuffers/upb/archive/8a3ae1ef3e3e3f26b45dec735c5776737fc7247f.tar.gz"],
        use_category = ["dataplane", "controlplane"],
        cpe = "N/A",
    ),
    kafka_source = dict(
        sha256 = "e7b748a62e432b5770db6dbb3b034c68c0ea212812cb51603ee7f3a8a35f06be",
        strip_prefix = "kafka-2.4.0/clients/src/main/resources/common/message",
        urls = ["https://github.com/apache/kafka/archive/2.4.0.zip"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:apache:kafka:*",
    ),
    kafka_server_binary = dict(
        sha256 = "b9582bab0c3e8d131953b1afa72d6885ca1caae0061c2623071e7f396f2ccfee",
        strip_prefix = "kafka_2.12-2.4.0",
        urls = ["http://us.mirrors.quenda.co/apache/kafka/2.4.0/kafka_2.12-2.4.0.tgz"],
        use_category = ["test"],
    ),
    kafka_python_client = dict(
        sha256 = "454bf3aafef9348017192417b7f0828a347ec2eaf3efba59336f3a3b68f10094",
        strip_prefix = "kafka-python-2.0.0",
        urls = ["https://github.com/dpkp/kafka-python/archive/2.0.0.tar.gz"],
        use_category = ["test"],
    ),
    org_unicode_icuuc = dict(
        strip_prefix = "icu-release-64-2",
        sha256 = "524960ac99d086cdb6988d2a92fc163436fd3c6ec0a84c475c6382fbf989be05",
        urls = ["https://github.com/unicode-org/icu/archive/release-64-2.tar.gz"],
        use_category = ["dataplane"],
        cpe = "cpe:2.3:a:icu-project:international_components_for_unicode",
    ),
)

REPOSITORY_LOCATIONS = dict(
    bazel_gazelle = dict(
        sha256 = "3c681998538231a2d24d0c07ed5a7658cb72bfb5fd4bf9911157c0e9ac6a2687",
        urls = ["https://github.com/bazelbuild/bazel-gazelle/releases/download/0.17.0/bazel-gazelle-0.17.0.tar.gz"],
    ),
    boringssl = dict(
        # Use commits from branch "chromium-stable-with-bazel"
        sha256 = "4825306f702fa5cb76fd86c987a88c9bbb241e75f4d86dbb3714530ca73c1fb1",
        strip_prefix = "boringssl-8cb07520451f0dc454654f2da5cdecf0b806f823",
        # chromium-74.0.3729.131
        urls = ["https://github.com/google/boringssl/archive/8cb07520451f0dc454654f2da5cdecf0b806f823.tar.gz"],
    ),
    boringssl_fips = dict(
        sha256 = "b12ad676ee533824f698741bd127f6fbc82c46344398a6d78d25e62c6c418c73",
        # fips-20180730
        urls = ["https://commondatastorage.googleapis.com/chromium-boringssl-docs/fips/boringssl-66005f41fbc3529ffe8d007708756720529da20d.tar.xz"],
    ),
    com_google_absl = dict(
        sha256 = "e35082e88b9da04f4d68094c05ba112502a5063712f3021adfa465306d238c76",
        strip_prefix = "abseil-cpp-cc8dcd307b76a575d2e3e0958a4fe4c7193c2f68",
        # 2018-10-31
        urls = ["https://github.com/abseil/abseil-cpp/archive/cc8dcd307b76a575d2e3e0958a4fe4c7193c2f68.tar.gz"],
    ),
    com_github_apache_thrift = dict(
        sha256 = "7d59ac4fdcb2c58037ebd4a9da5f9a49e3e034bf75b3f26d9fe48ba3d8806e6b",
        strip_prefix = "thrift-0.11.0",
        urls = ["https://files.pythonhosted.org/packages/c6/b4/510617906f8e0c5660e7d96fbc5585113f83ad547a3989b80297ac72a74c/thrift-0.11.0.tar.gz"],
    ),
    com_github_c_ares_c_ares = dict(
        sha256 = "7deb7872cbd876c29036d5f37e30c4cbc3cc068d59d8b749ef85bb0736649f04",
        strip_prefix = "c-ares-cares-1_15_0",
        urls = ["https://github.com/c-ares/c-ares/archive/cares-1_15_0.tar.gz"],
    ),
    com_github_circonus_labs_libcircllhist = dict(
        sha256 = "8165aa25e529d7d4b9ae849d3bf30371255a99d6db0421516abcff23214cdc2c",
        strip_prefix = "libcircllhist-63a16dd6f2fc7bc841bb17ff92be8318df60e2e1",
        # 2019-02-11
        urls = ["https://github.com/circonus-labs/libcircllhist/archive/63a16dd6f2fc7bc841bb17ff92be8318df60e2e1.tar.gz"],
    ),
    com_github_cyan4973_xxhash = dict(
        sha256 = "b34792646d5e19964bb7bba24f06cb13aecaac623ab91a54da08aa19d3686d7e",
        strip_prefix = "xxHash-0.7.0",
        urls = ["https://github.com/Cyan4973/xxHash/archive/v0.7.0.tar.gz"],
    ),
    com_github_envoyproxy_sqlparser = dict(
        sha256 = "425dfee0c4fe9aff8acf2365cde3dd2ba7fb878d2ba37562d33920e34c40c05e",
        strip_prefix = "sql-parser-5f50c68bdf5f107692bb027d1c568f67597f4d7f",
        urls = ["https://github.com/envoyproxy/sql-parser/archive/5f50c68bdf5f107692bb027d1c568f67597f4d7f.tar.gz"],
    ),
    com_github_eile_tclap = dict(
        sha256 = "f0ede0721dddbb5eba3a47385a6e8681b14f155e1129dd39d1a959411935098f",
        strip_prefix = "tclap-tclap-1-2-1-release-final",
        urls = ["https://github.com/eile/tclap/archive/tclap-1-2-1-release-final.tar.gz"],
    ),
    com_github_fmtlib_fmt = dict(
        sha256 = "4c0741e10183f75d7d6f730b8708a99b329b2f942dad5a9da3385ab92bb4a15c",
        strip_prefix = "fmt-5.3.0",
        urls = ["https://github.com/fmtlib/fmt/releases/download/5.3.0/fmt-5.3.0.zip"],
    ),
    com_github_gabime_spdlog = dict(
        sha256 = "160845266e94db1d4922ef755637f6901266731c4cb3b30b45bf41efa0e6ab70",
        strip_prefix = "spdlog-1.3.1",
        urls = ["https://github.com/gabime/spdlog/archive/v1.3.1.tar.gz"],
    ),
    com_github_gcovr_gcovr = dict(
        sha256 = "8a60ba6242d67a58320e9e16630d80448ef6d5284fda5fb3eff927b63c8b04a2",
        strip_prefix = "gcovr-3.3",
        urls = ["https://github.com/gcovr/gcovr/archive/3.3.tar.gz"],
    ),
    com_github_google_libprotobuf_mutator = dict(
        sha256 = "97b3639630040f41c45f45838ab00b78909e6b4cb69c8028e01302bea5b79495",
        strip_prefix = "libprotobuf-mutator-c3d2faf04a1070b0b852b0efdef81e1a81ba925e",
        # 2018-03-06
        urls = ["https://github.com/google/libprotobuf-mutator/archive/c3d2faf04a1070b0b852b0efdef81e1a81ba925e.tar.gz"],
    ),
    com_github_gperftools_gperftools = dict(
        # TODO(cmluciano): Bump to release 2.8
        # This sha is specifically chosen to fix ppc64le builds that require inclusion
        # of asm/ptrace.h
        sha256 = "18574813a062eee487bc1b761e8024a346075a7cb93da19607af362dc09565ef",
        strip_prefix = "gperftools-fc00474ddc21fff618fc3f009b46590e241e425e",
        urls = ["https://github.com/gperftools/gperftools/archive/fc00474ddc21fff618fc3f009b46590e241e425e.tar.gz"],
    ),
    com_github_grpc_grpc = dict(
        sha256 = "ba8b08a697b66e14af35da07753583cf32ff3d14dcd768f91b1bbe2e6c07c349",
        strip_prefix = "grpc-1.20.1",
        urls = ["https://github.com/grpc/grpc/archive/v1.20.1.tar.gz"],
    ),
    com_github_luajit_luajit = dict(
        sha256 = "409f7fe570d3c16558e594421c47bdd130238323c9d6fd6c83dedd2aaeb082a8",
        strip_prefix = "LuaJIT-2.1.0-beta3",
        urls = ["https://github.com/LuaJIT/LuaJIT/archive/v2.1.0-beta3.tar.gz"],
    ),
    com_github_nanopb_nanopb = dict(
        sha256 = "5fb4dab0b7f6a239908407fe07c9d03877cd0502abb637e38c41091cb9c1d438",
        strip_prefix = "nanopb-0.3.9.3",
        urls = ["https://github.com/nanopb/nanopb/archive/0.3.9.3.tar.gz"],
    ),
    com_github_nghttp2_nghttp2 = dict(
        sha256 = "fe9a75ec44e3a2e8f7f0cb83ad91e663bbc4c5085baf37b57ee2610846d7cf5d",
        strip_prefix = "nghttp2-1.38.0",
        urls = ["https://github.com/nghttp2/nghttp2/releases/download/v1.38.0/nghttp2-1.38.0.tar.gz"],
    ),
    io_opentracing_cpp = dict(
        sha256 = "015c4187f7a6426a2b5196f0ccd982aa87f010cf61f507ae3ce5c90523f92301",
        strip_prefix = "opentracing-cpp-1.5.1",
        urls = ["https://github.com/opentracing/opentracing-cpp/archive/v1.5.1.tar.gz"],
    ),
    com_lightstep_tracer_cpp = dict(
        sha256 = "defbf471facfebde6523ca1177529b63784893662d4ef2c60db074be8aef0634",
        strip_prefix = "lightstep-tracer-cpp-0.8.0",
        urls = ["https://github.com/lightstep/lightstep-tracer-cpp/archive/v0.8.0.tar.gz"],
    ),
    lightstep_vendored_googleapis = dict(
        sha256 = "d1ef4f790eeaa805e7b364de05b91f9eed66bd6ae46f1483bbf49c33d86998e5",
        strip_prefix = "googleapis-d6f78d948c53f3b400bb46996eb3084359914f9b",
        # From: https://github.com/lightstep/lightstep-tracer-cpp/blob/v0.8.0/lightstep-tracer-common/third_party/googleapis/README.lightstep-tracer-common#L6
        urls = ["https://github.com/googleapis/googleapis/archive/d6f78d948c53f3b400bb46996eb3084359914f9b.tar.gz"],
    ),
    com_github_datadog_dd_opentracing_cpp = dict(
        sha256 = "a3d1c03e7af570fa64c01df259e6e9bb78637a6bd9c65c6bf7e8703e466dc22f",
        strip_prefix = "dd-opentracing-cpp-0.4.2",
        urls = ["https://github.com/DataDog/dd-opentracing-cpp/archive/v0.4.2.tar.gz"],
    ),
    com_github_google_benchmark = dict(
        # TODO (moderation) change back to tarball method on next benchmark release
        sha256 = "0de43b6eaddd356f1d6cd164f73f37faf2f6c96fd684e1f7ea543ce49c1d144e",
        strip_prefix = "benchmark-505be96ab23056580a3a2315abba048f4428b04e",
        urls = ["https://github.com/google/benchmark/archive/505be96ab23056580a3a2315abba048f4428b04e.tar.gz"],
    ),
    com_github_libevent_libevent = dict(
        sha256 = "ab3af422b7e4c6d9276b3637d87edb6cf628fd91c9206260b759778c3a28b330",
        # This SHA includes the new "prepare" and "check" watchers, used for event loop performance
        # stats (see https://github.com/libevent/libevent/pull/793) and the fix for a race condition
        # in the watchers (see https://github.com/libevent/libevent/pull/802).
        # TODO(mergeconflict): Update to v2.2 when it is released.
        strip_prefix = "libevent-1cd8830de27c30c5324c75bfb6012c969c09ca2c",
        urls = ["https://github.com/libevent/libevent/archive/1cd8830de27c30c5324c75bfb6012c969c09ca2c.tar.gz"],
    ),
    com_github_madler_zlib = dict(
        sha256 = "629380c90a77b964d896ed37163f5c3a34f6e6d897311f1df2a7016355c45eff",
        strip_prefix = "zlib-1.2.11",
        urls = ["https://github.com/madler/zlib/archive/v1.2.11.tar.gz"],
    ),
    com_github_jbeder_yaml_cpp = dict(
        sha256 = "53dcffd55f3433b379fcc694f45c54898711c0e29159a7bd02e82a3e0253bac3",
        strip_prefix = "yaml-cpp-0f9a586ca1dc29c2ecb8dd715a315b93e3f40f79",
        urls = ["https://github.com/jbeder/yaml-cpp/archive/0f9a586ca1dc29c2ecb8dd715a315b93e3f40f79.tar.gz"],
    ),
    com_github_msgpack_msgpack_c = dict(
        sha256 = "bda49f996a73d2c6080ff0523e7b535917cd28c8a79c3a5da54fc29332d61d1e",
        strip_prefix = "msgpack-c-cpp-3.1.1",
        urls = ["https://github.com/msgpack/msgpack-c/archive/cpp-3.1.1.tar.gz"],
    ),
    com_github_google_jwt_verify = dict(
        sha256 = "700be26170c1917e83d1319b88a2112dccd1179cd78c5672940483e7c45ca6ae",
        strip_prefix = "jwt_verify_lib-85cf0edf1f1bc507ff7d96a8d6a9bc20307b0fcf",
        # 2018-12-01
        urls = ["https://github.com/google/jwt_verify_lib/archive/85cf0edf1f1bc507ff7d96a8d6a9bc20307b0fcf.tar.gz"],
    ),
    com_github_nodejs_http_parser = dict(
        sha256 = "ef26268c54c8084d17654ba2ed5140bffeffd2a040a895ffb22a6cca3f6c613f",
        strip_prefix = "http-parser-2.9.0",
        urls = ["https://github.com/nodejs/http-parser/archive/v2.9.0.tar.gz"],
    ),
    com_github_pallets_jinja = dict(
        sha256 = "e9baab084b8d84b511c75aca98bba8585041dbe971d5476ee53d9c6eea1b58b3",
        strip_prefix = "jinja-2.10.1",
        urls = ["https://github.com/pallets/jinja/archive/2.10.1.tar.gz"],
    ),
    com_github_pallets_markupsafe = dict(
        sha256 = "222a10e3237d92a9cd45ed5ea882626bc72bc5e0264d3ed0f2c9129fa69fc167",
        strip_prefix = "markupsafe-1.1.1/src",
        urls = ["https://github.com/pallets/markupsafe/archive/1.1.1.tar.gz"],
    ),
    com_github_tencent_rapidjson = dict(
        sha256 = "bf7ced29704a1e696fbccf2a2b4ea068e7774fa37f6d7dd4039d0787f8bed98e",
        strip_prefix = "rapidjson-1.1.0",
        urls = ["https://github.com/Tencent/rapidjson/archive/v1.1.0.tar.gz"],
    ),
    com_github_twitter_common_lang = dict(
        sha256 = "56d1d266fd4767941d11c27061a57bc1266a3342e551bde3780f9e9eb5ad0ed1",
        strip_prefix = "twitter.common.lang-0.3.9/src",
        urls = ["https://files.pythonhosted.org/packages/08/bc/d6409a813a9dccd4920a6262eb6e5889e90381453a5f58938ba4cf1d9420/twitter.common.lang-0.3.9.tar.gz"],
    ),
    com_github_twitter_common_rpc = dict(
        sha256 = "0792b63fb2fb32d970c2e9a409d3d00633190a22eb185145fe3d9067fdaa4514",
        strip_prefix = "twitter.common.rpc-0.3.9/src",
        urls = ["https://files.pythonhosted.org/packages/be/97/f5f701b703d0f25fbf148992cd58d55b4d08d3db785aad209255ee67e2d0/twitter.common.rpc-0.3.9.tar.gz"],
    ),
    com_github_twitter_common_finagle_thrift = dict(
        sha256 = "1e3a57d11f94f58745e6b83348ecd4fa74194618704f45444a15bc391fde497a",
        strip_prefix = "twitter.common.finagle-thrift-0.3.9/src",
        urls = ["https://files.pythonhosted.org/packages/f9/e7/4f80d582578f8489226370762d2cf6bc9381175d1929eba1754e03f70708/twitter.common.finagle-thrift-0.3.9.tar.gz"],
    ),
    com_google_googletest = dict(
        sha256 = "a4cb4b0c3ebb191b798594aca674ad47eee255dcb4c26885cf7f49777703484f",
        strip_prefix = "googletest-eb9225ce361affe561592e0912320b9db84985d0",
        # TODO(akonradi): Switch this back to a released version later than 1.8.1 once there is
        # one available.
        urls = ["https://github.com/google/googletest/archive/eb9225ce361affe561592e0912320b9db84985d0.tar.gz"],
    ),
    com_google_protobuf = dict(
        sha256 = "c10ef8d8ad5a9e5f850483051b7f9ee2c8bb3ca2e0e16a4cf105bd1321afb2d6",
        strip_prefix = "protobuf-3.7.1",
        urls = ["https://github.com/protocolbuffers/protobuf/releases/download/v3.7.1/protobuf-all-3.7.1.tar.gz"],
    ),
    grpc_httpjson_transcoding = dict(
        sha256 = "dedd76b0169eb8c72e479529301a1d9b914a4ccb4d2b5ddb4ebe92d63a7b2152",
        strip_prefix = "grpc-httpjson-transcoding-64d6ac985360b624d8e95105701b64a3814794cd",
        # 2018-12-19
        urls = ["https://github.com/grpc-ecosystem/grpc-httpjson-transcoding/archive/64d6ac985360b624d8e95105701b64a3814794cd.tar.gz"],
    ),
    com_github_golang_protobuf = dict(
        # TODO(sesmith177): Remove this dependency when:
        #   1. There's a release of rules_go that includes golang/protobuf v1.3.1
        sha256 = "3f3a6123054a9847093c119895f1660612f301fe95358f3a6a1a33fd0933e6cf",
        strip_prefix = "protobuf-1.3.1",
        urls = ["https://github.com/golang/protobuf/archive/v1.3.1.tar.gz"],
    ),
    io_bazel_rules_go = dict(
        sha256 = "91b79f4758fd16f2c6426279ce00c1d2d8577d61c519db39675ed84657e1a95e",
        urls = ["https://github.com/bazelbuild/rules_go/releases/download/0.17.4/rules_go-0.17.4.tar.gz"],
    ),
    rules_foreign_cc = dict(
        sha256 = "136470a38dcd00c7890230402b43004dc947bf1e3dd0289dd1bd2bfb1e0a3484",
        strip_prefix = "rules_foreign_cc-e3f4b5e0bc9dac9cf036616c13de25e6cd5051a2",
        # 2019-04-04
        urls = ["https://github.com/bazelbuild/rules_foreign_cc/archive/e3f4b5e0bc9dac9cf036616c13de25e6cd5051a2.tar.gz"],
    ),
    six_archive = dict(
        sha256 = "105f8d68616f8248e24bf0e9372ef04d3cc10104f1980f54d57b2ce73a5ad56a",
        urls = ["https://pypi.python.org/packages/source/s/six/six-1.10.0.tar.gz#md5=34eed507548117b2ab523ab14b2f8b55"],
    ),
    # I'd love to name this `com_github_google_subpar`, but something in the Subpar
    # code assumes its repository name is just `subpar`.
    subpar = dict(
        sha256 = "eddbfc920e9cd565500370114316757848b672deba06dc2336acfa81b4ac0e6d",
        strip_prefix = "subpar-1.3.0",
        urls = ["https://github.com/google/subpar/archive/1.3.0.tar.gz"],
    ),
    com_googlesource_quiche = dict(
        # Static snapshot of https://quiche.googlesource.com/quiche/+archive/7bf7c3c358eb954e463bde14ea27444f4bd8ea05.tar.gz
        sha256 = "36fe180d532a9ccb18cd32328af5231636c7408104523f9ed5eebbad75f1e039",
        urls = ["https://storage.googleapis.com/quiche-envoy-integration/7bf7c3c358eb954e463bde14ea27444f4bd8ea05.tar.gz"],
    ),
)

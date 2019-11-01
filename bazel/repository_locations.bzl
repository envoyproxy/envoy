REPOSITORY_LOCATIONS = dict(
    bazel_compdb = dict(
        sha256 = "801b35d996a097d223e028815fdba8667bf62bc5efb353486603d31fc2ba6ff9",
        strip_prefix = "bazel-compilation-database-0.4.1",
        urls = ["https://github.com/grailbio/bazel-compilation-database/archive/0.4.1.tar.gz"],
    ),
    bazel_gazelle = dict(
        sha256 = "41bff2a0b32b02f20c227d234aa25ef3783998e5453f7eade929704dcff7cd4b",
        urls = ["https://github.com/bazelbuild/bazel-gazelle/releases/download/v0.19.0/bazel-gazelle-v0.19.0.tar.gz"],
    ),
    bazel_toolchains = dict(
        sha256 = "1e16833a9f0e32b292568c0dfee7bd48133c2038605757d3a430551394310006",
        strip_prefix = "bazel-toolchains-1.1.0",
        urls = [
            "https://github.com/bazelbuild/bazel-toolchains/releases/download/1.1.0/bazel-toolchains-1.1.0.tar.gz",
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/archive/1.1.0.tar.gz",
        ],
    ),
    envoy_build_tools = dict(
        sha256 = "d0f88bef8bd7f76c3684407977f5673f3d06a6c50d4ddaffb8f0e7df6b0ef69e",
        strip_prefix = "envoy-build-tools-a6b28555badcb18d6be924c8fc1bea49971656b8",
        urls = ["https://github.com/envoyproxy/envoy-build-tools/archive/a6b28555badcb18d6be924c8fc1bea49971656b8.tar.gz"],
    ),
    boringssl = dict(
        sha256 = "891352824e0f7977bc0c291b8c65076e3ed23630334841b93f346f12d4484b06",
        strip_prefix = "boringssl-5565939d4203234ddc742c02241ce4523e7b3beb",
        # To update BoringSSL, which tracks Chromium releases:
        # 1. Open https://omahaproxy.appspot.com/ and note <current_version> of linux/beta release.
        # 2. Open https://chromium.googlesource.com/chromium/src/+/refs/tags/<current_version>/DEPS and note <boringssl_revision>.
        # 3. Find a commit in BoringSSL's "master-with-bazel" branch that merges <boringssl_revision>.
        #
        # chromium-78.0.3904.21 (BETA)
        urls = ["https://github.com/google/boringssl/archive/5565939d4203234ddc742c02241ce4523e7b3beb.tar.gz"],
    ),
    boringssl_fips = dict(
        sha256 = "b12ad676ee533824f698741bd127f6fbc82c46344398a6d78d25e62c6c418c73",
        # fips-20180730
        urls = ["https://commondatastorage.googleapis.com/chromium-boringssl-docs/fips/boringssl-66005f41fbc3529ffe8d007708756720529da20d.tar.xz"],
    ),
    com_google_absl = dict(
        sha256 = "3df5970908ed9a09ba51388d04661803a6af18c373866f442cede7f381e0b94a",
        strip_prefix = "abseil-cpp-14550beb3b7b97195e483fb74b5efb906395c31e",
        # 2019-07-31
        urls = ["https://github.com/abseil/abseil-cpp/archive/14550beb3b7b97195e483fb74b5efb906395c31e.tar.gz"],
    ),
    com_github_apache_thrift = dict(
        sha256 = "7d59ac4fdcb2c58037ebd4a9da5f9a49e3e034bf75b3f26d9fe48ba3d8806e6b",
        strip_prefix = "thrift-0.11.0",
        urls = ["https://files.pythonhosted.org/packages/c6/b4/510617906f8e0c5660e7d96fbc5585113f83ad547a3989b80297ac72a74c/thrift-0.11.0.tar.gz"],
    ),
    com_github_c_ares_c_ares = dict(
        sha256 = "bbaab13d6ad399a278d476f533e4d88a7ec7d729507348bb9c2e3b207ba4c606",
        strip_prefix = "c-ares-d7e070e7283f822b1d2787903cce3615536c5610",
        # 2019-06-19
        # 27 new commits from release-1.15.0. Upgrade for commit 7d3591ee8a1a63e7748e68e6d880bd1763a32885 "getaddrinfo enhancements" and follow up fixes.
        # Use getaddrinfo to query DNS record and TTL.
        # TODO(crazyxy): Update to release-1.16.0 when it is released.
        urls = ["https://github.com/c-ares/c-ares/archive/d7e070e7283f822b1d2787903cce3615536c5610.tar.gz"],
    ),
    com_github_circonus_labs_libcircllhist = dict(
        sha256 = "8165aa25e529d7d4b9ae849d3bf30371255a99d6db0421516abcff23214cdc2c",
        strip_prefix = "libcircllhist-63a16dd6f2fc7bc841bb17ff92be8318df60e2e1",
        # 2019-02-11
        urls = ["https://github.com/circonus-labs/libcircllhist/archive/63a16dd6f2fc7bc841bb17ff92be8318df60e2e1.tar.gz"],
    ),
    com_github_cyan4973_xxhash = dict(
        sha256 = "7e93d28e81c3e95ff07674a400001d0cdf23b7842d49b211e5582d00d8e3ac3e",
        strip_prefix = "xxHash-0.7.2",
        urls = ["https://github.com/Cyan4973/xxHash/archive/v0.7.2.tar.gz"],
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
    com_github_google_libprotobuf_mutator = dict(
        sha256 = "f45c3ad82376d891cd0bcaa7165e83efd90e0014b00aebf0cbaf07eb05a1d3f9",
        strip_prefix = "libprotobuf-mutator-d1fe8a7d8ae18f3d454f055eba5213c291986f21",
        # 2019-07-10
        urls = ["https://github.com/google/libprotobuf-mutator/archive/d1fe8a7d8ae18f3d454f055eba5213c291986f21.tar.gz"],
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
        sha256 = "cce1d4585dd017980d4a407d8c5e9f8fc8c1dbb03f249b99e88a387ebb45a035",
        strip_prefix = "grpc-1.22.1",
        urls = ["https://github.com/grpc/grpc/archive/v1.22.1.tar.gz"],
    ),
    com_github_luajit_luajit = dict(
        sha256 = "409f7fe570d3c16558e594421c47bdd130238323c9d6fd6c83dedd2aaeb082a8",
        strip_prefix = "LuaJIT-2.1.0-beta3",
        urls = ["https://github.com/LuaJIT/LuaJIT/archive/v2.1.0-beta3.tar.gz"],
    ),
    com_github_nanopb_nanopb = dict(
        sha256 = "cbc8fba028635d959033c9ba8d8186a713165e94a9de02a030a20b3e64866a04",
        strip_prefix = "nanopb-0.3.9.4",
        urls = ["https://github.com/nanopb/nanopb/archive/0.3.9.4.tar.gz"],
    ),
    com_github_nghttp2_nghttp2 = dict(
        sha256 = "25b623cd04dc6a863ca3b34ed6247844effe1aa5458229590b3f56a6d53cd692",
        strip_prefix = "nghttp2-1.39.1",
        urls = ["https://github.com/nghttp2/nghttp2/releases/download/v1.39.1/nghttp2-1.39.1.tar.gz"],
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
    com_github_datadog_dd_opentracing_cpp = dict(
        sha256 = "052fd37cd698e24ab73ee18fc3fa55acd1d43153c12a0e65b0fba0447de1117e",
        strip_prefix = "dd-opentracing-cpp-1.1.1",
        urls = ["https://github.com/DataDog/dd-opentracing-cpp/archive/v1.1.1.tar.gz"],
    ),
    com_github_google_benchmark = dict(
        sha256 = "3c6a165b6ecc948967a1ead710d4a181d7b0fbcaa183ef7ea84604994966221a",
        strip_prefix = "benchmark-1.5.0",
        urls = ["https://github.com/google/benchmark/archive/v1.5.0.tar.gz"],
    ),
    com_github_libevent_libevent = dict(
        sha256 = "549d34065eb2485dfad6c8de638caaa6616ed130eec36dd978f73b6bdd5af113",
        # This SHA includes the new "prepare" and "check" watchers, used for event loop performance
        # stats (see https://github.com/libevent/libevent/pull/793) and the fix for a race condition
        # in the watchers (see https://github.com/libevent/libevent/pull/802).
        # This also includes the fixes for https://github.com/libevent/libevent/issues/806
        # and https://github.com/lyft/envoy-mobile/issues/215.
        # TODO(mergeconflict): Update to v2.2 when it is released.
        strip_prefix = "libevent-0d7d85c2083f7a4c9efe01c061486f332b576d28",
        # 2019-07-02
        urls = ["https://github.com/libevent/libevent/archive/0d7d85c2083f7a4c9efe01c061486f332b576d28.tar.gz"],
    ),
    net_zlib = dict(
        # Use the dev branch of zlib to resolve fuzz bugs and out of bound
        # errors resulting in crashes in zlib 1.2.11.
        # TODO(asraa): Remove when zlib > 1.2.11 is released.
        sha256 = "155a8f8c1a753fb05b16a1b0cc0a0a9f61a78e245f9e0da483d13043b3bcbf2e",
        strip_prefix = "zlib-79baebe50e4d6b73ae1f8b603f0ef41300110aa3",
        # 2019-04-14 development branch
        urls = ["https://github.com/madler/zlib/archive/79baebe50e4d6b73ae1f8b603f0ef41300110aa3.tar.gz"],
    ),
    com_github_jbeder_yaml_cpp = dict(
        sha256 = "77ea1b90b3718aa0c324207cb29418f5bced2354c2e483a9523d98c3460af1ed",
        strip_prefix = "yaml-cpp-yaml-cpp-0.6.3",
        urls = ["https://github.com/jbeder/yaml-cpp/archive/yaml-cpp-0.6.3.tar.gz"],
    ),
    com_github_msgpack_msgpack_c = dict(
        sha256 = "fbaa28c363a316fd7523f31d1745cf03eab0d1e1ea5a1c60aa0dffd4ce551afe",
        strip_prefix = "msgpack-3.2.0",
        urls = ["https://github.com/msgpack/msgpack-c/releases/download/cpp-3.2.0/msgpack-3.2.0.tar.gz"],
    ),
    com_github_google_jwt_verify = dict(
        sha256 = "480ff74bcdea9e177a803feb8f797c76abd38a80ec27d93b64f4d56e7cfd28a1",
        strip_prefix = "jwt_verify_lib-9f10e2d60d42edeb6662e185707a7d6a4ebc5604",
        # 2019-10-24
        urls = ["https://github.com/google/jwt_verify_lib/archive/9f10e2d60d42edeb6662e185707a7d6a4ebc5604.tar.gz"],
    ),
    com_github_nodejs_http_parser = dict(
        sha256 = "ef26268c54c8084d17654ba2ed5140bffeffd2a040a895ffb22a6cca3f6c613f",
        strip_prefix = "http-parser-2.9.0",
        urls = ["https://github.com/nodejs/http-parser/archive/v2.9.0.tar.gz"],
    ),
    com_github_pallets_jinja = dict(
        sha256 = "db49236731373e4f3118af880eb91bb0aa6978bc0cf8b35760f6a026f1a9ffc4",
        strip_prefix = "jinja-2.10.3",
        urls = ["https://github.com/pallets/jinja/archive/2.10.3.tar.gz"],
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
        sha256 = "cbd251a40485fddd44cdf641af6df2953d45695853af6d68aeb11c7efcde6771",
        strip_prefix = "googletest-d7003576dd133856432e2e07340f45926242cc3a",
        # 2019-07-16
        # TODO(akonradi): Switch this back to a released version later than 1.8.1 once there is
        # one available.
        urls = ["https://github.com/google/googletest/archive/d7003576dd133856432e2e07340f45926242cc3a.tar.gz"],
    ),
    com_google_protobuf = dict(
        sha256 = "7c99ddfe0227cbf6a75d1e75b194e0db2f672d2d2ea88fb06bdc83fe0af4c06d",
        strip_prefix = "protobuf-3.9.2",
        urls = ["https://github.com/protocolbuffers/protobuf/releases/download/v3.9.2/protobuf-all-3.9.2.tar.gz"],
    ),
    grpc_httpjson_transcoding = dict(
        sha256 = "a447458b47ea4dc1d31499f555769af437c5d129d988ec1e13d5fdd0a6a36b4e",
        strip_prefix = "grpc-httpjson-transcoding-2feabd5d64436e670084091a937855972ee35161",
        # 2019-08-28
        urls = ["https://github.com/grpc-ecosystem/grpc-httpjson-transcoding/archive/2feabd5d64436e670084091a937855972ee35161.tar.gz"],
    ),
    io_bazel_rules_go = dict(
        sha256 = "842ec0e6b4fbfdd3de6150b61af92901eeb73681fd4d185746644c338f51d4c0",
        urls = ["https://github.com/bazelbuild/rules_go/releases/download/v0.20.1/rules_go-v0.20.1.tar.gz"],
    ),
    rules_foreign_cc = dict(
        sha256 = "3184c244b32e65637a74213fc448964b687390eeeca42a36286f874c046bba15",
        strip_prefix = "rules_foreign_cc-7bc4be735b0560289f6b86ab6136ee25d20b65b7",
        # 2019-09-26
        urls = ["https://github.com/bazelbuild/rules_foreign_cc/archive/7bc4be735b0560289f6b86ab6136ee25d20b65b7.tar.gz"],
    ),
    rules_proto = dict(
        sha256 = "602e7161d9195e50246177e7c55b2f39950a9cf7366f74ed5f22fd45750cd208",
        strip_prefix = "rules_proto-97d8af4dc474595af3900dd85cb3a29ad28cc313",
        # 2019-08-02
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/rules_proto/archive/97d8af4dc474595af3900dd85cb3a29ad28cc313.tar.gz",
            "https://github.com/bazelbuild/rules_proto/archive/97d8af4dc474595af3900dd85cb3a29ad28cc313.tar.gz",
        ],
    ),
    six_archive = dict(
        sha256 = "105f8d68616f8248e24bf0e9372ef04d3cc10104f1980f54d57b2ce73a5ad56a",
        urls = ["https://files.pythonhosted.org/packages/b3/b2/238e2590826bfdd113244a40d9d3eb26918bd798fc187e2360a8367068db/six-1.10.0.tar.gz"],
    ),
    io_opencensus_cpp = dict(
        sha256 = "c95ab57835182b8b4b17cf5bbfc2406805bc78c5022c17399f3e5c643f22826a",
        strip_prefix = "opencensus-cpp-98970f78091ae65b4a029bcf512696ba6d665cf4",
        # 2019-09-24
        urls = ["https://github.com/census-instrumentation/opencensus-cpp/archive/98970f78091ae65b4a029bcf512696ba6d665cf4.tar.gz"],
    ),
    com_github_curl = dict(
        sha256 = "d0393da38ac74ffac67313072d7fe75b1fa1010eb5987f63f349b024a36b7ffb",
        strip_prefix = "curl-7.66.0",
        urls = ["https://github.com/curl/curl/releases/download/curl-7_66_0/curl-7.66.0.tar.gz"],
    ),
    com_googlesource_quiche = dict(
        # Static snapshot of https://quiche.googlesource.com/quiche/+archive/4abb566fbbc63df8fe7c1ac30b21632b9eb18d0c.tar.gz
        sha256 = "c60bca3cf7f58b91394a89da96080657ff0fbe4d5675be9b21e90da8f68bc06f",
        urls = ["https://storage.googleapis.com/quiche-envoy-integration/4abb566fbbc63df8fe7c1ac30b21632b9eb18d0c.tar.gz"],
    ),
    com_google_cel_cpp = dict(
        sha256 = "e21d11be5eca677fe79839d310ceffb2f950d9d03f7682af8c0d311e573a1302",
        strip_prefix = "cel-cpp-d85f82972c2def6db9c90f3d9a23f56a0ac3caff",
        # 2019-10-23
        urls = ["https://github.com/google/cel-cpp/archive/d85f82972c2def6db9c90f3d9a23f56a0ac3caff.tar.gz"],
    ),
    com_googlesource_code_re2 = dict(
        sha256 = "b0382aa7369f373a0148218f2df5a6afd6bfa884ce4da2dfb576b979989e615e",
        strip_prefix = "re2-2019-09-01",
        urls = ["https://github.com/google/re2/archive/2019-09-01.tar.gz"],
    ),
    # Included to access FuzzedDataProvider.h. This is compiler agnostic but
    # provided as part of the compiler-rt source distribution. We can't use the
    # Clang variant as we are not a Clang-LLVM only shop today.
    org_llvm_releases_compiler_rt = dict(
        sha256 = "56e4cd96dd1d8c346b07b4d6b255f976570c6f2389697347a6c3dcb9e820d10e",
        # Only allow peeking at fuzzer related files for now.
        strip_prefix = "compiler-rt-9.0.0.src/lib/fuzzer",
        urls = ["http://releases.llvm.org/9.0.0/compiler-rt-9.0.0.src.tar.xz"],
    ),
    fuzzit_linux = dict(
        sha256 = "9ca76ac1c22d9360936006efddf992977ebf8e4788ded8e5f9d511285c9ac774",
        urls = ["https://github.com/fuzzitdev/fuzzit/releases/download/v2.4.76/fuzzit_Linux_x86_64.zip"],
    ),
)

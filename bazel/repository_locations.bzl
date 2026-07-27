# This should match the schema defined in external_deps.bzl.

PROTOBUF_VERSION = "35.1"

# These names of these deps *must* match the names used in `/bazel/protobuf.patch`,
# and both must match the names from the protobuf releases (see
# https://github.com/protocolbuffers/protobuf/releases).
# The names change in upcoming versions.
# The shas are calculated from the downloads on the releases page.
PROTOC_VERSIONS = dict(
    linux_aarch_64 = "01bf9d08808c7f96678b63f4bd8efa559bb4f83d5a7a270d5edaf507f9d5d9cf",
    linux_x86_64 = "6930ebf62bd4ea607b98fff052596c6ee564b9835b4ce172c75a3f53ae9d91b7",
    linux_ppcle_64 = "92da6d454ca3c30b0acf9bd3613dde973a179855742b1ca2859f30a4555cd6e5",
    osx_aarch_64 = "193289af0470c6a1aada357d4fba0bbf8d78bfaac8b5e42ca30af2ef75583de2",
    osx_x86_64 = "537d73604a344ded6fc94e98e07e529d4fe3e4a0b09e59905353950fafc2a1f7",
    win64 = "5d3ff218d7d91eea95f7569bcb5a98f3030f8996d44151279d9772edcff76082",
)

REPOSITORY_LOCATIONS_SPEC = dict(
    bazel_compdb = dict(
        version = "40864791135333e1446a04553b63cbe744d358d0",
        sha256 = "acd2a9eaf49272bb1480c67d99b82662f005b596a8c11739046a4220ec73c4da",
        strip_prefix = "bazel-compilation-database-{version}",
        urls = ["https://github.com/grailbio/bazel-compilation-database/archive/{version}.tar.gz"],
    ),
    bazel_features = dict(
        version = "1.51.0",
        sha256 = "5450bfb2c8b4bc961c75368838f86156f563cc9adef1be7d504fc5619d54daab",
        urls = ["https://github.com/bazel-contrib/bazel_features/releases/download/v{version}/bazel_features-v{version}.tar.gz"],
        strip_prefix = "bazel_features-{version}",
    ),
    bazel_gazelle = dict(
        version = "0.47.0",
        sha256 = "675114d8b433d0a9f54d81171833be96ebc4113115664b791e6f204d58e93446",
        urls = ["https://github.com/bazelbuild/bazel-gazelle/releases/download/v{version}/bazel-gazelle-v{version}.tar.gz"],
    ),
    build_bazel_rules_apple = dict(
        version = "3.20.1",
        sha256 = "73ad768dfe824c736d0a8a81521867b1fb7a822acda2ed265897c03de6ae6767",
        urls = ["https://github.com/bazelbuild/rules_apple/releases/download/{version}/rules_apple.{version}.tar.gz"],
    ),
    buildtools = dict(
        version = "8.5.1",
        sha256 = "f3b800e9f6ca60bdef3709440f393348f7c18a29f30814288a7326285c80aab9",
        strip_prefix = "buildtools-{version}",
        urls = ["https://github.com/bazelbuild/buildtools/archive/v{version}.tar.gz"],
    ),
    envoy_toolshed = dict(
        version = "0.3.36",
        sha256 = "7e1e9575d9477254e8855c4483b8fb08b088bd49868c3ef9330a4bb196492542",
        strip_prefix = "toolshed-bazel-v{version}",
        urls = ["https://github.com/envoyproxy/toolshed/releases/download/bazel-v{version}/toolshed-bazel-v{version}.tar.gz"],
    ),
    rules_fuzzing = dict(
        # Patch contains workaround for https://github.com/bazelbuild/rules_python/issues/1221
        version = "0.8.0",
        sha256 = "7d13a78e9edb1f63093adec75e16c47e2b975382b8911fc53b72f2355fcd0a16",
        strip_prefix = "rules_fuzzing-{version}",
        urls = ["https://github.com/bazelbuild/rules_fuzzing/archive/v{version}.tar.gz"],
    ),
    boringssl = dict(
        # To update BoringSSL, which tracks BCR tags, open https://registry.bazel.build/modules/boringssl
        # and select an appropriate tag for the new version.
        version = "0.20260413.0",
        sha256 = "3560f7dd3f08e16b9f84d877a5be21ec62071564783009571af5fcc6fad734d2",
        strip_prefix = "boringssl-{version}",
        urls = ["https://github.com/google/boringssl/archive/{version}.tar.gz"],
    ),
    openssl = dict(
        version = "3.5.7",
        sha256 = "a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8",
        strip_prefix = "openssl-{version}",
        urls = ["https://github.com/openssl/openssl/releases/download/openssl-{version}/openssl-{version}.tar.gz"],
    ),
    aspect_bazel_lib = dict(
        version = "2.21.2",
        sha256 = "53cadea9109e646a93ed4dc90c9bbcaa8073c7c3df745b92f6a5000daf7aa3da",
        strip_prefix = "bazel-lib-{version}",
        urls = ["https://github.com/aspect-build/bazel-lib/releases/download/v{version}/bazel-lib-v{version}.tar.gz"],
    ),
    abseil_cpp = dict(
        version = "20260107.1",
        sha256 = "4314e2a7cbac89cac25a2f2322870f343d81579756ceff7f431803c2c9090195",
        strip_prefix = "abseil-cpp-{version}",
        urls = ["https://github.com/abseil/abseil-cpp/archive/{version}.tar.gz"],
    ),
    shellcheck = dict(
        version = "0.4.0",
        sha256 = "cef935ea1088d2b45c5bc3630f8178c91ba367b071af2bfdcd16c042c5efe8ae",
        strip_prefix = "rules_shellcheck-{version}",
        urls = ["https://github.com/aignas/rules_shellcheck/archive/{version}.tar.gz"],
    ),
    aws_c_auth_testdata = dict(
        version = "0.10.4",
        sha256 = "6fb567f496a450d4b6d3f5749d735977a0156957e8ccbca9af7a5ee15d1ffda7",
        strip_prefix = "aws-c-auth-{version}",
        urls = ["https://github.com/awslabs/aws-c-auth/archive/refs/tags/v{version}.tar.gz"],
    ),
    liburing = dict(
        version = "2.15",
        sha256 = "8d052f2622dcb3678cbaee5ff582a87572672a6c0a56533cdda5b65cb636120a",
        strip_prefix = "liburing-liburing-{version}",
        urls = ["https://github.com/axboe/liburing/archive/liburing-{version}.tar.gz"],
    ),
    # libelf, used by libbpf for the sockmap socket interface. Built only when sockmap is enabled
    # with --define=sockmap=enabled. Never built for releases.
    elfutils = dict(
        version = "0.195",
        sha256 = "37629fdf7f1f3dc2818e138fca2b8094177d6c2d0f701d3bb650a561218dc026",
        strip_prefix = "elfutils-{version}",
        urls = ["https://sourceware.org/elfutils/ftp/{version}/elfutils-{version}.tar.bz2"],
    ),
    # eBPF loader for the sockmap socket interface. Built only when sockmap is enabled with
    # --define=sockmap=enabled. Never built for releases.
    libbpf = dict(
        version = "1.7.0",
        sha256 = "7ab5feffbf78557f626f2e3e3204788528394494715a30fc2070fcddc2051b7b",
        strip_prefix = "libbpf-{version}",
        urls = ["https://github.com/libbpf/libbpf/archive/refs/tags/v{version}.tar.gz"],
    ),
    # This dependency is built only when performance tracing is enabled with the
    # option --define=perf_tracing=enabled. It's never built for releases.
    perfetto = dict(
        version = "57.2",
        sha256 = "c6fa3d89aee30f7da39402c9cd178c9f2e344544fda5c2109fd8457e319c3a2f",
        urls = ["https://github.com/google/perfetto/releases/download/v{version}/perfetto-cpp-sdk-src.zip"],
    ),
    c_ares = dict(
        version = "1.34.8",
        sha256 = "c222b6d681096f9444d2c4863d2c1174019e27cacca0a4a5c114d36dd7d7bf78",
        strip_prefix = "c-ares-{version}",
        urls = ["https://github.com/c-ares/c-ares/releases/download/v{version}/c-ares-{version}.tar.gz"],
    ),
    libcircllhist = dict(
        version = "0.3.2",
        sha256 = "6dfbd649fde380f7a2256def43b9c6374c6d6fe768178b09e39eedf874b139f4",
        strip_prefix = "libcircllhist-py-{version}",
        urls = ["https://github.com/openhistogram/libcircllhist/archive/refs/tags/py-{version}.tar.gz"],
    ),
    xxhash = dict(
        version = "0.8.3",
        sha256 = "aae608dfe8213dfd05d909a57718ef82f30722c392344583d3f39050c7f29a80",
        strip_prefix = "xxHash-{version}",
        urls = ["https://github.com/Cyan4973/xxHash/archive/v{version}.tar.gz"],
    ),
    sql_parser = dict(
        version = "52e5ad1f4fbb21301fcee7f9d18eef7e6ae6ab3e",
        sha256 = "011e4786048436b90384cd2124a6ab4dd1961ae026df8d9dab9286e95a1e295f",
        strip_prefix = "sql-parser-{version}",
        urls = ["https://github.com/envoyproxy/sql-parser/archive/{version}.tar.gz"],
    ),
    tclap = dict(
        version = "1.2.5",
        sha256 = "7e87d13734076fa4f626f6144ce9a02717198b3f054341a6886e2107b048b235",
        strip_prefix = "tclap-{version}",
        urls = ["https://github.com/mirror/tclap/archive/v{version}.tar.gz"],
    ),
    fmt = dict(
        version = "12.2.0",
        sha256 = "a2f4a8d51178f954e4c339007f77edd76ba0cb2e36f87a48e5a5403d9be5878f",
        strip_prefix = "fmt-{version}",
        urls = ["https://github.com/fmtlib/fmt/releases/download/{version}/fmt-{version}.zip"],
    ),
    spdlog = dict(
        version = "1.17.0",
        sha256 = "d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744",
        strip_prefix = "spdlog-{version}",
        urls = ["https://github.com/gabime/spdlog/archive/v{version}.tar.gz"],
    ),
    libprotobuf_mutator = dict(
        version = "1.5",
        sha256 = "895958881b4993df70b4f652c2d82c5bd97d22f801ca4f430d6546809df293d5",
        strip_prefix = "libprotobuf-mutator-{version}",
        urls = ["https://github.com/google/libprotobuf-mutator/archive/v{version}.tar.gz"],
    ),
    libsxg = dict(
        version = "beaa3939b76f8644f6833267e9f2462760838f18",
        sha256 = "082bf844047a9aeec0d388283d5edc68bd22bcf4d32eb5a566654ae89956ad1f",
        strip_prefix = "libsxg-{version}",
        urls = ["https://github.com/google/libsxg/archive/{version}.tar.gz"],
    ),
    tcmalloc = dict(
        version = "12f255231938d30493186b0a037feedd70f5a1c1",
        sha256 = "2a6bef88f8cccda4a63a2f4bb09e655b3ee5ea0a2ce68d16e6ea2d5f5c4be9c1",
        strip_prefix = "tcmalloc-{version}",
        urls = ["https://github.com/google/tcmalloc/archive/{version}.tar.gz"],
    ),
    gperftools = dict(
        version = "2.18.1",
        sha256 = "d18d919175f9e4d740ace6b52f0f4f91284160c454e91b36ffd6456282a02206",
        strip_prefix = "gperftools-{version}",
        urls = ["https://github.com/gperftools/gperftools/releases/download/gperftools-{version}/gperftools-{version}.tar.gz"],
    ),
    jemalloc = dict(
        version = "5.3.0",
        sha256 = "2db82d1e7119df3e71b7640219b6dfe84789bc0537983c3b7ac4f7189aecfeaa",
        strip_prefix = "jemalloc-{version}",
        urls = ["https://github.com/jemalloc/jemalloc/releases/download/{version}/jemalloc-{version}.tar.bz2"],
    ),
    com_github_grpc_grpc = dict(
        version = "1.83.0",
        sha256 = "90d453393a9d41215df546103b10b33b9566df79cdf6f49dc67f6c4d044d090d",
        strip_prefix = "grpc-{version}",
        urls = ["https://github.com/grpc/grpc/archive/v{version}.tar.gz"],
    ),
    rules_proto_grpc = dict(
        version = "4.6.0",
        sha256 = "2a0860a336ae836b54671cbbe0710eec17c64ef70c4c5a88ccfd47ea6e3739bd",
        strip_prefix = "rules_proto_grpc-{version}",
        urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/releases/download/{version}/rules_proto_grpc-{version}.tar.gz"],
    ),
    icu = dict(
        # When this is updated, make sure to update the icu.patch patch file and remove
        # all remaining Bazel build artifacts (for example WORKSPACE and BUILD.bazel files)
        # from the icu source code, to prevent Bazel from treating the foreign library
        # as a Bazel project.
        # https://github.com/envoyproxy/envoy/issues/26395
        version = "78.2",
        sha256 = "af38c3d4904e47e1bc2dd7587922ee2aec312fefa677804582e3fecca3edb272",
        strip_prefix = "icu",
        urls = ["https://github.com/unicode-org/icu/releases/download/release-{version}/icu4c-{version}-sources.zip"],
    ),
    ipp_crypto = dict(
        version = "2.2.0",
        sha256 = "d43503c479dd3292173b55aa6777161461e45621c0003169ea1a3b29aba897ed",
        strip_prefix = "cryptography-primitives-{version}",
        urls = ["https://github.com/intel/cryptography-primitives/archive/refs/tags/v{version}.tar.gz"],
    ),
    numactl = dict(
        version = "2.0.19",
        sha256 = "8b84ffdebfa0d730fb2fc71bb7ec96bb2d38bf76fb67246fde416a68e04125e4",
        strip_prefix = "numactl-{version}",
        urls = ["https://github.com/numactl/numactl/archive/refs/tags/v{version}.tar.gz"],
    ),
    uadk = dict(
        version = "2.9",
        sha256 = "857339dd270d1fb3c068eae0f912c8814d3490b7ff25e6ef200086fce2b57557",
        strip_prefix = "uadk-{version}",
        urls = ["https://github.com/Linaro/uadk/archive/refs/tags/v{version}.tar.gz"],
    ),
    qatlib = dict(
        version = "26.02.0",
        sha256 = "7c68bf05f196153b1b1669a7d17e5bfba6253e7cafb69f67d30a0d17e7facecb",
        strip_prefix = "qatlib-{version}",
        urls = ["https://github.com/intel/qatlib/archive/refs/tags/{version}.tar.gz"],
    ),
    qatzip = dict(
        version = "1.3.2",
        sha256 = "36cf711dbe8027d1cdf420e87a70ed4394383cd2443dc6465bc8210594206756",
        strip_prefix = "QATzip-{version}",
        urls = ["https://github.com/intel/QATzip/archive/v{version}.tar.gz"],
    ),
    qat_zstd = dict(
        version = "1.0.0",
        sha256 = "00f2611719f0a1c9585965c6c3c1fe599119aa8e932a569041b1876ffc944fb3",
        strip_prefix = "QAT-ZSTD-Plugin-{version}",
        urls = ["https://github.com/intel/QAT-ZSTD-Plugin/archive/refs/tags/v{version}.tar.gz"],
    ),
    luajit = dict(
        # LuaJIT only provides rolling releases
        version = "871db2c84ecefd70a850e03a6c340214a81739f0",
        sha256 = "ab3f16d82df6946543565cfb0d2810d387d79a3a43e0431695b03466188e2680",
        strip_prefix = "LuaJIT-{version}",
        urls = ["https://github.com/LuaJIT/LuaJIT/archive/{version}.tar.gz"],
    ),
    nghttp2 = dict(
        version = "1.66.0",
        sha256 = "e178687730c207f3a659730096df192b52d3752786c068b8e5ee7aeb8edae05a",
        strip_prefix = "nghttp2-{version}",
        urls = ["https://github.com/nghttp2/nghttp2/releases/download/v{version}/nghttp2-{version}.tar.gz"],
    ),
    hyperscan = dict(
        version = "5.4.2",
        sha256 = "32b0f24b3113bbc46b6bfaa05cf7cf45840b6b59333d078cc1f624e4c40b2b99",
        strip_prefix = "hyperscan-{version}",
        urls = ["https://github.com/intel/hyperscan/archive/v{version}.tar.gz"],
    ),
    vectorscan = dict(
        version = "5.4.11",
        sha256 = "905f76ad1fa9e4ae0eb28232cac98afdb96c479666202c5a4c27871fb30a2711",
        strip_prefix = "vectorscan-vectorscan-{version}",
        urls = ["https://codeload.github.com/VectorCamp/vectorscan/tar.gz/refs/tags/vectorscan/{version}"],
    ),
    opentelemetry_cpp = dict(
        version = "1.28.0",
        sha256 = "8c359919175d77c502515f5a783907d031cc6a172e44426dbe9bee3c1532201e",
        strip_prefix = "opentelemetry-cpp-{version}",
        urls = ["https://github.com/open-telemetry/opentelemetry-cpp/archive/refs/tags/v{version}.tar.gz"],
    ),
    skywalking_data_collect_protocol = dict(
        sha256 = "70e63dd30d9dfdcf0a0ef0976e19c7e21fa1411af13ec31452bd1da37e578e35",
        urls = ["https://github.com/apache/skywalking-data-collect-protocol/archive/v{version}.tar.gz"],
        strip_prefix = "skywalking-data-collect-protocol-{version}",
        version = "10.4.0",
    ),
    cpp2sky = dict(
        sha256 = "d7e52f517de5a1dc7d927dd63a2e5aa5cf8c2dcfd8fcf6b64e179978daf1c3ed",
        version = "0.6.0",
        strip_prefix = "cpp2sky-{version}",
        urls = ["https://github.com/SkyAPM/cpp2sky/archive/v{version}.tar.gz"],
    ),
    dd_trace_cpp = dict(
        version = "2.1.1",
        sha256 = "3e8d2a12c434b766ac92c1fcec07f49b92b8a12902f97e6c54839a65abe44649",
        strip_prefix = "dd-trace-cpp-{version}",
        urls = ["https://github.com/DataDog/dd-trace-cpp/archive/v{version}.tar.gz"],
    ),
    benchmark = dict(
        version = "1.9.5",
        sha256 = "9631341c82bac4a288bef951f8b26b41f69021794184ece969f8473977eaa340",
        strip_prefix = "benchmark-{version}",
        urls = ["https://github.com/google/benchmark/archive/v{version}.tar.gz"],
    ),
    libevent = dict(
        version = "release-2.2.2-alpha",
        sha256 = "f0a1ead383fb4992cde92dfea88a19e43d8198b592938e454da1f5b7dbc39da9",
        strip_prefix = "libevent-{version}",
        urls = ["https://github.com/libevent/libevent/archive/{version}.tar.gz"],
    ),
    colm = dict(
        # The latest release version v0.14.7 prevents building statically (see
        # https://github.com/adrian-thurston/colm/issues/146). The latest SHA includes the fix (see
        # https://github.com/adrian-thurston/colm/commit/fc61ecb3a22b89864916ec538eaf04840e7dd6b5).
        # TODO(zhxie): Update to the next release version when it is released.
        version = "2d8ba76ddaf6634f285d0a81ee42d5ee77d084cf",
        sha256 = "f11e62f0e7fd8b26f75a9034af43fd4622a0829b29a7cfb70c0742959bd9cfec",
        strip_prefix = "colm-suite-{version}",
        urls = ["https://github.com/adrian-thurston/colm-suite/archive/{version}.tar.gz"],
    ),
    ragel = dict(
        # We used the stable release Ragel 6.10 previously and it is under GPLv2 license (see
        # http://www.colm.net/open-source/ragel). Envoy uses its binary only as a tool for
        # compiling contrib extension Hyperscan. For copyright consideration, we update Ragel to
        # its development release which is under MIT license.
        # The latest release version v7.0.4 is not compatible with its dependency Colm we use. The
        # latest SHA includes fix for compatibility.
        # TODO(zhxie): Update to the next release version when it is released.
        version = "d4577c924451b331c73c8ed0af04f6efd35ac0b4",
        sha256 = "fa3474d50da9c870b79b51ad43f8d11cdf05268f5ec05a602ecd5b1b5f5febb0",
        strip_prefix = "ragel-{version}",
        urls = ["https://github.com/adrian-thurston/ragel/archive/{version}.tar.gz"],
    ),
    boost = dict(
        version = "1.89.0",
        sha256 = "9de758db755e8330a01d995b0a24d09798048400ac25c03fc5ea9be364b13c93",
        strip_prefix = "boost_{underscore_version}",
        urls = ["https://archives.boost.io/release/{version}/source/boost_{underscore_version}.tar.gz"],
    ),
    brotli = dict(
        version = "1.2.0",
        sha256 = "816c96e8e8f193b40151dad7e8ff37b1221d019dbcb9c35cd3fadbfe6477dfec",
        strip_prefix = "brotli-{version}",
        urls = ["https://github.com/google/brotli/archive/v{version}.tar.gz"],
    ),
    zstd = dict(
        version = "1.5.7",
        sha256 = "37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3",
        strip_prefix = "zstd-{version}",
        urls = ["https://github.com/facebook/zstd/archive/v{version}.tar.gz"],
    ),
    zlib_ng = dict(
        version = "2.3.2",
        sha256 = "6a0561b50b8f5f6434a6a9e667a67026f2b2064a1ffa959c6b2dae320161c2a8",
        strip_prefix = "zlib-ng-{version}",
        urls = ["https://github.com/zlib-ng/zlib-ng/archive/{version}.tar.gz"],
    ),
    yaml_cpp = dict(
        version = "0.9.0",
        sha256 = "25cb043240f828a8c51beb830569634bc7ac603978e0f69d6b63558dadefd49a",
        strip_prefix = "yaml-cpp-yaml-cpp-{version}",
        urls = ["https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-{version}.tar.gz"],
        # YAML is also used for runtime as well as controlplane. It shouldn't appear on the
        # dataplane but we can't verify this automatically due to code structure today.
    ),
    msgpack_cxx = dict(
        version = "7.0.0",
        sha256 = "7504b7af7e7b9002ce529d4f941e1b7fb1fb435768780ce7da4abaac79bb156f",
        strip_prefix = "msgpack-cxx-{version}",
        urls = ["https://github.com/msgpack/msgpack-c/releases/download/cpp-{version}/msgpack-cxx-{version}.tar.gz"],
    ),
    hessian2_codec = dict(
        version = "6f5a64770f0374a761eece13c8863b80dc5adcd8",
        sha256 = "bb4c4af6a7e3031160bf38dfa957b0ee950e2d8de47d4ba14c7a658c3a2c74d1",
        strip_prefix = "hessian2-codec-{version}",
        urls = ["https://github.com/alibaba/hessian2-codec/archive/{version}.tar.gz"],
    ),
    nlohmann_json = dict(
        version = "3.12.0",
        sha256 = "4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187",
        strip_prefix = "json-{version}",
        urls = ["https://github.com/nlohmann/json/archive/v{version}.tar.gz"],
        # This has replaced rapidJSON used in extensions and may also be a fast
        # replacement for protobuf JSON.
    ),
    # This is an external dependency needed while running the
    # envoy docker image. A bazel target has been created since
    # there is no binary package available for the utility on Ubuntu
    # which is the base image used to build an envoy container.
    # This is not needed to build an envoy binary or run tests.
    su_exec = dict(
        version = "0.3",
        sha256 = "1de7479857879b6d14772792375290a87eac9a37b0524d39739a4a0739039620",
        strip_prefix = "su-exec-{version}",
        urls = ["https://github.com/ncopa/su-exec/archive/v{version}.tar.gz"],
    ),
    googletest = dict(
        version = "1.17.0",
        sha256 = "65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c",
        strip_prefix = "googletest-{version}",
        urls = ["https://github.com/google/googletest/releases/download/v{version}/googletest-{version}.tar.gz"],
    ),
    com_google_protobuf = dict(
        version = PROTOBUF_VERSION,
        # When upgrading the protobuf library, please re-run
        # test/common/json:gen_excluded_unicodes to recompute the ranges
        # excluded from differential fuzzing that are populated in
        # test/common/json/json_sanitizer_test_util.cc.
        sha256 = "f0b6838e7522a8da96126d487068c959bc624926368f3024ac8fd03abd0a1ac4",
        strip_prefix = "protobuf-{version}",
        urls = ["https://github.com/protocolbuffers/protobuf/releases/download/v{version}/protobuf-{version}.tar.gz"],
    ),
    grpc_httpjson_transcoding = dict(
        version = "a6e226f9a2e656a973df3ad48f0ee5efacce1a28",
        sha256 = "45dc1a630f518df21b4e044e32b27c7b02ae77ef401b48a20e5ffde0f070113f",
        strip_prefix = "grpc-httpjson-transcoding-{version}",
        urls = ["https://github.com/grpc-ecosystem/grpc-httpjson-transcoding/archive/{version}.tar.gz"],
    ),
    proto_converter = dict(
        version = "1db76535b86b80aa97489a1edcc7009e18b67ab7",
        sha256 = "9555d9cf7bd541ea5fdb67d7d6b72ea44da77df3e27b960b4155dc0c6b81d476",
        strip_prefix = "proto-converter-{version}",
        urls = ["https://github.com/grpc-ecosystem/proto-converter/archive/{version}.zip"],
    ),
    proto_field_extraction = dict(
        version = "d5d39f0373e9b6691c32c85929838b1006bcb3fb",
        sha256 = "cba864db90806515afa553aaa2fb3683df2859a7535e53a32cb9619da9cebc59",
        strip_prefix = "proto-field-extraction-{version}",
        urls = ["https://github.com/grpc-ecosystem/proto-field-extraction/archive/{version}.zip"],
    ),
    proto_processing = dict(
        version = "279353cfab372ac7f268ae529df29c4d546ca18d",
        sha256 = "bac7a0d02fd8533cd5ce6d0f39dc324fc0565702d85a9ee3b65b0be8e7cbdd8d",
        strip_prefix = "proto_processing_lib-{version}",
        urls = ["https://github.com/grpc-ecosystem/proto_processing_lib/archive/{version}.zip"],
    ),
    ocp = dict(
        version = "e965ac0ac6db6686169678e2a6c77ede904fa82c",
        sha256 = "b83b8ea7a937ce7f5d6870421be8f9a5343e8c2de2bd2e269452981d67da1509",
        strip_prefix = "ocp-diag-core-{version}/apis/c++",
        urls = ["https://github.com/opencomputeproject/ocp-diag-core/archive/{version}.zip"],
    ),
    io_bazel_rules_go = dict(
        version = "0.61.1",
        sha256 = "763f4a3f6b03469fdb00a77a333dd0b5546d3ee1fa29db373128c08fee73e0e8",
        urls = ["https://github.com/bazelbuild/rules_go/releases/download/v{version}/rules_go-v{version}.zip"],
    ),
    rules_cc = dict(
        version = "0.2.22",
        sha256 = "81c10a95a5c22d838276ee90d712635d6042419fdfca5ef88328226b6321e53b",
        strip_prefix = "rules_cc-{version}",
        urls = ["https://github.com/bazelbuild/rules_cc/releases/download/{version}/rules_cc-{version}.tar.gz"],
    ),
    rules_foreign_cc = dict(
        version = "0.15.1",
        sha256 = "32759728913c376ba45b0116869b71b68b1c2ebf8f2bcf7b41222bc07b773d73",
        strip_prefix = "rules_foreign_cc-{version}",
        urls = ["https://github.com/bazelbuild/rules_foreign_cc/archive/{version}.tar.gz"],
    ),
    rules_java = dict(
        version = "9.7.0",
        sha256 = "68794ca344c1caf13dca65f90c06660823013fa080931266e2625103904a664e",
        urls = ["https://github.com/bazelbuild/rules_java/releases/download/{version}/rules_java-{version}.tar.gz"],
    ),
    rules_python = dict(
        version = "2.2.0",
        sha256 = "e11d2e1efce1589e5bdfa93986712c74fc7467a0f093143d489d2ef5ebb1ed2a",
        strip_prefix = "rules_python-{version}",
        urls = ["https://github.com/bazelbuild/rules_python/archive/{version}.tar.gz"],
    ),
    rules_ruby = dict(
        # This is needed only to compile protobuf, not used in Envoy code
        version = "37cf5900d0b0e44fa379c0ea3f5fcee0035d77ca",
        sha256 = "24ed42b7e06907be993b21be603c130076c7d7e49d4f4d5bd228c5656a257f5e",
        strip_prefix = "rules_ruby-{version}",
        urls = ["https://github.com/protocolbuffers/rules_ruby/archive/{version}.tar.gz"],
    ),
    rules_pkg = dict(
        version = "1.2.0",
        sha256 = "de9c6411e8f1b45dd67ce2b93e9243731aec37df6d65cc20dcd574d2d0b65d0f",
        strip_prefix = "rules_pkg-{version}",
        urls = ["https://github.com/bazelbuild/rules_pkg/archive/{version}.tar.gz"],
    ),
    rules_shell = dict(
        version = "0.8.0",
        sha256 = "20721f63908879c083f94869e618ea8d4ff5edb91ff9a72a2ebee357fdbc352d",
        strip_prefix = "rules_shell-{version}",
        urls = ["https://github.com/bazelbuild/rules_shell/releases/download/v{version}/rules_shell-v{version}.tar.gz"],
    ),
    wamr = dict(
        version = "WAMR-2.4.4",
        sha256 = "03ad51037f06235577b765ee042a462326d8919300107af4546719c35525b298",
        strip_prefix = "wasm-micro-runtime-{version}",
        urls = ["https://github.com/bytecodealliance/wasm-micro-runtime/archive/{version}.tar.gz"],
    ),
    wasmtime = dict(
        version = "45.0.2",
        sha256 = "a95cf57008b87dbe1cdaba220ea58b75bb0b53369e166fe21e729011bd25e9e8",
        strip_prefix = "wasmtime-{version}",
        urls = ["https://github.com/bytecodealliance/wasmtime/archive/v{version}.tar.gz"],
    ),
    v8 = dict(
        # NOTE: Update together with proxy_wasm_cpp_host, highway, fast_float, dragonbox, simdutf, and fp16.
        # Patch contains workaround for https://github.com/bazelbuild/rules_python/issues/1221
        version = "14.6.202.10",
        # Follow this guide to pick next stable release: https://v8.dev/docs/version-numbers#which-v8-version-should-i-use%3F
        strip_prefix = "v8-{version}",
        sha256 = "09c3d9f796a671fb9630c7190032f00171ce99effd7c80c7aaeba148a7bcbc1b",
        urls = ["https://github.com/v8/v8/archive/refs/tags/{version}.tar.gz"],
    ),
    fast_float = dict(
        # NOTE: Update together with v8 and proxy_wasm_cpp_host.
        version = "7.0.0",
        # Follow this guide to pick next stable release: https://v8.dev/docs/version-numbers#which-v8-version-should-i-use%3F
        strip_prefix = "fast_float-{version}",
        sha256 = "d2a08e722f461fe699ba61392cd29e6b23be013d0f56e50c7786d0954bffcb17",
        urls = ["https://github.com/fastfloat/fast_float/archive/refs/tags/v{version}.tar.gz"],
    ),
    highway = dict(
        # NOTE: Update together with v8 and proxy_wasm_cpp_host.
        version = "1.2.0",
        strip_prefix = "highway-{version}",
        sha256 = "7e0be78b8318e8bdbf6fa545d2ecb4c90f947df03f7aadc42c1967f019e63343",
        urls = ["https://github.com/google/highway/archive/refs/tags/{version}.tar.gz"],
    ),
    dragonbox = dict(
        # NOTE: Update together with v8 and proxy_wasm_cpp_host.
        version = "6c7c925b571d54486b9ffae8d9d18a822801cbda",
        strip_prefix = "dragonbox-{version}",
        sha256 = "2f10448d665355b41f599e869ac78803f82f13b070ce7ef5ae7b5cceb8a178f3",
        urls = ["https://github.com/jk-jeon/dragonbox/archive/{version}.zip"],
    ),
    fp16 = dict(
        # NOTE: Update together with v8 and proxy_wasm_cpp_host.
        version = "3d2de1816307bac63c16a297e8c4dc501b4076df",
        strip_prefix = "FP16-{version}",
        sha256 = "e2da4f41bae8869f8dee56f4c104e699e7de3a483b5e451fda8e76fbcc66c59a",
        urls = ["https://github.com/Maratyszcza/FP16/archive/{version}.zip"],
    ),
    simdutf = dict(
        # NOTE: Update together with v8 and proxy_wasm_cpp_host.
        version = "8.1.0",
        sha256 = "c3565a8567b21d0096d0366654db473597ea6e5408e464198dce0897be71e4d0",
        urls = ["https://github.com/simdutf/simdutf/releases/download/v{version}/singleheader.zip"],
    ),
    quiche = dict(
        version = "0580a14c23b7f7005abd2c18587f108ed6f1e93e",
        sha256 = "30a8bbb156d5e3739dc19741837df2d8191d18dc0506727f08f2db5f88a72328",
        urls = ["https://github.com/google/quiche/archive/{version}.tar.gz"],
        strip_prefix = "quiche-{version}",
    ),
    googleurl = dict(
        version = "dd4080fec0b443296c0ed0036e1e776df8813aa7",
        sha256 = "4ffa45a827646692e7b26e2a8c0dcbc1b1763a26def2fbbd82362970962a2fcf",
        urls = ["https://github.com/google/gurl/archive/{version}.tar.gz"],
        strip_prefix = "gurl-{version}",
    ),
    cel_spec = dict(
        version = "0.25.2",
        sha256 = "6bd7bbf973c6cd56136e7a5ec8efe61b7656fc52813350bc2807224551f515e7",
        strip_prefix = "cel-spec-{version}",
        urls = ["https://github.com/google/cel-spec/archive/v{version}.tar.gz"],
    ),
    cel_cpp = dict(
        version = "0.14.0",
        sha256 = "0a4f9a1c0bcc83629eb30d1c278883d32dec0f701efcdabd7bebf33fef8dab71",
        strip_prefix = "cel-cpp-{version}",
        urls = ["https://github.com/google/cel-cpp/archive/v{version}.tar.gz"],
    ),
    flatbuffers = dict(
        version = "25.12.19",
        sha256 = "f81c3162b1046fe8b84b9a0dbdd383e24fdbcf88583b9cb6028f90d04d90696a",
        strip_prefix = "flatbuffers-{version}",
        urls = ["https://github.com/google/flatbuffers/archive/v{version}.tar.gz"],
    ),
    re2 = dict(
        version = "2024-07-02",
        sha256 = "a835fe55fbdcd8e80f38584ab22d0840662c67f2feb36bd679402da9641dc71e",
        strip_prefix = "re2-{version}",
        urls = ["https://github.com/google/re2/releases/download/{version}/re2-{version}.zip"],
    ),
    kafka_source = dict(
        version = "3.9.2",
        sha256 = "085bbee8208fea6d1cb22498afa6ffc3835037516643208f705420564b49b80e",
        strip_prefix = "kafka-{version}/clients/src/main/resources/common/message",
        urls = ["https://github.com/apache/kafka/archive/{version}.zip"],
    ),
    confluentinc_librdkafka = dict(
        version = "2.6.0",
        sha256 = "abe0212ecd3e7ed3c4818a4f2baf7bf916e845e902bb15ae48834ca2d36ac745",
        strip_prefix = "librdkafka-{version}",
        urls = ["https://github.com/confluentinc/librdkafka/archive/v{version}.tar.gz"],
    ),
    proxy_wasm_cpp_sdk = dict(
        version = "e5256b0c5463ea9961965ad5de3e379e00486640",
        sha256 = "b560a1da27a0d3ab374527e9c7dfa4fe6493887299945be2762a0518ce35570e",
        strip_prefix = "proxy-wasm-cpp-sdk-{version}",
        urls = ["https://github.com/proxy-wasm/proxy-wasm-cpp-sdk/archive/{version}.tar.gz"],
    ),
    proxy_wasm_cpp_host = dict(
        version = "f2db56af443571e92a31c0b877106d9ea96e19ef",
        sha256 = "34dac5bcebf0b156e435bf8dd9bdac5be60b95f967c420c680578d73af28c604",
        strip_prefix = "proxy-wasm-cpp-host-{version}",
        urls = ["https://github.com/proxy-wasm/proxy-wasm-cpp-host/archive/{version}.tar.gz"],
    ),
    proxy_wasm_rust_sdk = dict(
        version = "0.2.4",
        sha256 = "e407f6aaf58437d5ea23393823163fd2b6bf4fac6adb6b8ace6561b1a7afeac6",
        strip_prefix = "proxy-wasm-rust-sdk-{version}",
        urls = ["https://github.com/proxy-wasm/proxy-wasm-rust-sdk/archive/v{version}.tar.gz"],
    ),
    emsdk = dict(
        version = "4.0.6",
        sha256 = "2d3292d508b4f5477f490b080b38a34aaefed43e85258a1de72cb8dde3f8f3af",
        strip_prefix = "emsdk-{version}/bazel",
        urls = ["https://github.com/emscripten-core/emsdk/archive/refs/tags/{version}.tar.gz"],
    ),
    # NOTE: Required for rules_rust 0.67.0 compatibility with Bazel 7.x.
    # This provides the UEFI platform constraint used by rules_rust.
    # May be removable once Envoy upgrades to Bazel 8.0+ which includes this by default.
    # See: https://github.com/envoyproxy/envoy/pull/41172#issuecomment-2365923085
    platforms = dict(
        version = "1.1.0",
        sha256 = "324f5381753a610e472f79563d44e2026438195042aae4dc660b8c021f7de7f5",
        strip_prefix = "platforms-{version}",
        urls = [
            "https://github.com/bazelbuild/platforms/archive/{version}.tar.gz",
        ],
    ),
    # After updating you may need to run:
    #
    #     CARGO_BAZEL_REPIN=1 bazel sync --only=crate_index
    #
    rules_rust = dict(
        version = "0.69.0",
        sha256 = "bbc764c252d061281b2359277a4d46480e2dcfaf72afc1ce6e00ada58ccbfd4c",
        # Note: rules_rust should point to the releases, not archive to avoid the hassle of bootstrapping in crate_universe.
        # This is described in https://bazelbuild.github.io/rules_rust/crate_universe.html#setup, otherwise bootstrap
        # is required which in turn requires a system CC toolchains, not the bazel controlled ones.
        urls = ["https://github.com/bazelbuild/rules_rust/releases/download/{version}/rules_rust-{version}.tar.gz"],
    ),
    vpp_vcl = dict(
        version = "85abefb55ee931fa4e45c0b6a9fc8c43118651b3",
        sha256 = "5624c4a4407285d9f269d0041ed4fc8d5fa3664abc22850069236b026f97d3f2",
        strip_prefix = "vpp-{version}",
        urls = ["https://github.com/FDio/vpp/archive/{version}.tar.gz"],
    ),
    dlb = dict(
        version = "8.8.0",
        sha256 = "564534254ef32bfed56e0a464c53fca0907e446b30929c253210e2c3d6de58b9",
        urls = ["https://downloadmirror.intel.com/819078/dlb_linux_src_release_8.8.0.txz"],
    ),
    libpfm = dict(
        version = "4.11.0",
        sha256 = "bd49c66c1854f9a5f347725c298fce39d7ac2644b2dfc3237e67d91c9c0d7823",
        strip_prefix = "libpfm4-{version}",
        urls = ["https://github.com/wcohen/libpfm4/archive/refs/tags/v{version}.tar.gz"],
    ),
    rules_license = dict(
        version = "1.0.0",
        sha256 = "26d4021f6898e23b82ef953078389dd49ac2b5618ac564ade4ef87cced147b38",
        urls = ["https://github.com/bazelbuild/rules_license/releases/download/{version}/rules_license-{version}.tar.gz"],
    ),
    libmaxminddb = dict(
        version = "1.13.3",
        sha256 = "a66502ea76eadbe17f2cd6fd708946777253972d2ae8157dee1b23a2fb528171",
        strip_prefix = "libmaxminddb-{version}",
        urls = ["https://github.com/maxmind/libmaxminddb/releases/download/{version}/libmaxminddb-{version}.tar.gz"],
    ),
    thrift = dict(
        version = "0.24.0",
        sha256 = "d3a60676b1df3fb9850f6ce1e8a3df9a0efac036ceb4bce8ce46ee3eca735d2d",
        strip_prefix = "thrift-{version}/lib/py/",
        urls = ["https://github.com/apache/thrift/archive/refs/tags/v{version}.tar.gz"],
    ),
    toolchains_llvm = dict(
        version = "1.7.0",
        sha256 = "85c341e957ba58482892a8088e4a34391d15bd98917f0993ecb62f008d6986d6",
        strip_prefix = "toolchains_llvm-v{version}",
        urls = ["https://github.com/bazel-contrib/toolchains_llvm/releases/download/v{version}/toolchains_llvm-v{version}.tar.gz"],
    ),
    lz4 = dict(
        version = "1.10.0",
        sha256 = "537512904744b35e232912055ccf8ec66d768639ff3abe5788d90d792ec5f48b",
        strip_prefix = "lz4-{version}",
        urls = ["https://github.com/lz4/lz4/archive/v{version}.tar.gz"],
    ),
    yq_bzl = dict(
        version = "0.1.1",
        sha256 = "b51d82b561a78ab21d265107b0edbf98d68a390b4103992d0b03258bb3819601",
        strip_prefix = "yq.bzl-{version}",
        urls = ["https://github.com/bazel-contrib/yq.bzl/releases/download/v{version}/yq.bzl-v{version}.tar.gz"],
    ),
    # Dependencies for fips - VERSIONS SHOULD NOT BE CHANGED
    fips_ninja = dict(
        version = "1.13.2",
        sha256 = "974d6b2f4eeefa25625d34da3cb36bdcebe7fbce40f4c16ac0835fd1c0cbae17",
        strip_prefix = "ninja-{version}",
        urls = ["https://github.com/ninja-build/ninja/archive/refs/tags/v{version}.tar.gz"],
    ),
    fips_cmake_linux_x86_64 = dict(
        version = "4.4.0",
        sha256 = "3864eb649b4466ae126a64bbde1657adad78efbbaa068bf38201de5cf1b5349f",
        strip_prefix = "cmake-{version}-linux-x86_64",
        urls = ["https://github.com/Kitware/CMake/releases/download/v{version}/cmake-{version}-linux-x86_64.tar.gz"],
    ),
    fips_cmake_linux_aarch64 = dict(
        version = "4.4.0",
        sha256 = "e98bb53e0b00a8f672424517d34c05bb9b94fd1c888c89e0b81bc8df51d1a94b",
        strip_prefix = "cmake-{version}-linux-aarch64",
        urls = ["https://github.com/Kitware/CMake/releases/download/v{version}/cmake-{version}-linux-aarch64.tar.gz"],
    ),
    fips_go_linux_amd64 = dict(
        version = "1.26.3",
        sha256 = "2b2cfc7148493da5e73981bffbf3353af381d5f93e789c82c79aff64962eb556",
        strip_prefix = "go",
        urls = ["https://dl.google.com/go/go{version}.linux-amd64.tar.gz"],
    ),
    fips_go_linux_arm64 = dict(
        version = "1.26.3",
        sha256 = "9d89a3ea57d141c2b22d70083f2c8459ba3890f2d9e818e7e933b75614936565",
        strip_prefix = "go",
        urls = ["https://dl.google.com/go/go{version}.linux-arm64.tar.gz"],
    ),
    wuffs = dict(
        version = "0.4.0-alpha.9",
        sha256 = "9ca4f5401a76be244362de8b39138f01f2456c444b03584703a9f1db90491ba6",
        strip_prefix = "wuffs-mirror-release-c-{version}",
        urls = ["https://github.com/google/wuffs-mirror-release-c/archive/refs/tags/v{version}.tar.gz"],
        # Wuffs: memory-safe, high-performance JSON (and other format) parser.
        # The amalgamated C file at release/c/wuffs-v0.4.c is both the header
        # (declarations) and implementation (when WUFFS_IMPLEMENTATION is defined).
    ),
)

def _compiled_protoc_deps(locations, versions):
    for platform, sha in versions.items():
        locations["com_google_protobuf_protoc_%s" % platform] = dict(
            version = PROTOBUF_VERSION,
            sha256 = sha,
            urls = ["https://github.com/protocolbuffers/protobuf/releases/download/v{version}/protoc-{version}-%s.zip" % platform.replace("_", "-", 1)],
        )

_compiled_protoc_deps(REPOSITORY_LOCATIONS_SPEC, PROTOC_VERSIONS)

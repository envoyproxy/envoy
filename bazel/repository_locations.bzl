# This should match the schema defined in external_deps.bzl.

PROTOBUF_VERSION = "33.2"

# These names of these deps *must* match the names used in `/bazel/protobuf.patch`,
# and both must match the names from the protobuf releases (see
# https://github.com/protocolbuffers/protobuf/releases).
# The names change in upcoming versions.
# The shas are calculated from the downloads on the releases page.
PROTOC_VERSIONS = dict(
    linux_aarch_64 = "706662a332683aa2fffe1c4ea61588279d31679cd42d91c7d60a69651768edb8",
    linux_x86_64 = "b24b53f87c151bfd48b112fe4c3a6e6574e5198874f38036aff41df3456b8caf",
    linux_ppcle_64 = "16b4a36c07daab458bc040523b1f333ddd37e1440fa71634f297a458c7fef4c4",
    osx_aarch_64 = "5be1427127788c9f7dd7d606c3e69843dd3587327dea993917ffcb77e7234b44",
    osx_x86_64 = "dba51cfcc85076d56e7de01a647865c5a7f995c3dce427d5215b53e50b7be43f",
    win64 = "376770cd4073beb63db56fdd339260edb9957b3c4472e05a75f5f9ec8f98d8f5",
)

REPOSITORY_LOCATIONS_SPEC = dict(
    bazel_compdb = dict(
        version = "40864791135333e1446a04553b63cbe744d358d0",
        sha256 = "acd2a9eaf49272bb1480c67d99b82662f005b596a8c11739046a4220ec73c4da",
        strip_prefix = "bazel-compilation-database-{version}",
        urls = ["https://github.com/grailbio/bazel-compilation-database/archive/{version}.tar.gz"],
    ),
    bazel_features = dict(
        version = "1.39.0",
        sha256 = "5ab1a90d09fd74555e0df22809ad589627ddff263cff82535815aa80ca3e3562",
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
    com_github_bazelbuild_buildtools = dict(
        version = "8.2.1",
        sha256 = "53119397bbce1cd7e4c590e117dcda343c2086199de62932106c80733526c261",
        strip_prefix = "buildtools-{version}",
        urls = ["https://github.com/bazelbuild/buildtools/archive/v{version}.tar.gz"],
    ),
    envoy_examples = dict(
        version = "0.1.8",
        sha256 = "7a350305da51972f2acb13ccf8fd2c8d165f9e161cb41d85f49495e425898dc8",
        strip_prefix = "examples-{version}",
        urls = ["https://github.com/envoyproxy/examples/archive/v{version}.tar.gz"],
    ),
    envoy_toolshed = dict(
        version = "0.3.28",
        sha256 = "8f4c01104e41eefaf08cbe7a678f50a2aff00e45ba873a2c0d3514e9b2debdb8",
        strip_prefix = "toolshed-bazel-v{version}/bazel",
        urls = ["https://github.com/envoyproxy/toolshed/archive/bazel-v{version}.tar.gz"],
    ),
    rules_fuzzing = dict(
        # Patch contains workaround for https://github.com/bazelbuild/rules_python/issues/1221
        version = "0.7.0",
        sha256 = "87adb1357bb5a932fa0de6fed0fc37412490a0c96f5259855008f208cf53a74f",
        strip_prefix = "rules_fuzzing-{version}",
        urls = ["https://github.com/bazelbuild/rules_fuzzing/archive/v{version}.tar.gz"],
    ),
    boringssl = dict(
        # To update BoringSSL, which tracks BCR tags, open https://registry.bazel.build/modules/boringssl
        # and select an appropriate tag for the new version.
        version = "0.20251124.0",
        sha256 = "d47f89b894bf534c82071d7426c5abf1e5bd044fee242def53cd5d3d0f656c09",
        strip_prefix = "boringssl-{version}",
        urls = ["https://github.com/google/boringssl/archive/{version}.tar.gz"],
    ),
    aws_lc = dict(
        version = "1.66.2",
        sha256 = "d64a46b4f75fa5362da412f1e96ff5b77eed76b3a95685651f81a558c5c9e126",
        strip_prefix = "aws-lc-{version}",
        urls = ["https://github.com/aws/aws-lc/archive/v{version}.tar.gz"],
    ),
    aspect_bazel_lib = dict(
        version = "2.21.2",
        sha256 = "079e47b2fd357396a376dec6ad7907a51028cb2b98233b45e5269cd5ce2fea51",
        strip_prefix = "bazel-lib-{version}",
        urls = ["https://github.com/aspect-build/bazel-lib/archive/v{version}.tar.gz"],
    ),
    com_google_absl = dict(
        version = "20250814.1",
        sha256 = "1692f77d1739bacf3f94337188b78583cf09bab7e420d2dc6c5605a4f86785a1",
        strip_prefix = "abseil-cpp-{version}",
        urls = ["https://github.com/abseil/abseil-cpp/archive/{version}.tar.gz"],
    ),
    com_github_aignas_rules_shellcheck = dict(
        version = "0.4.0",
        sha256 = "cef935ea1088d2b45c5bc3630f8178c91ba367b071af2bfdcd16c042c5efe8ae",
        strip_prefix = "rules_shellcheck-{version}",
        urls = ["https://github.com/aignas/rules_shellcheck/archive/{version}.tar.gz"],
    ),
    com_github_awslabs_aws_c_auth = dict(
        version = "0.9.5",
        sha256 = "39000bff55fe8c82265b9044a966ab37da5c192a775e1b68b6fcba7e7f9882fb",
        strip_prefix = "aws-c-auth-{version}",
        urls = ["https://github.com/awslabs/aws-c-auth/archive/refs/tags/v{version}.tar.gz"],
    ),
    com_github_axboe_liburing = dict(
        version = "2.13",
        sha256 = "618e34dbea408fc9e33d7c4babd746036dbdedf7fce2496b1178ced0f9b5b357",
        strip_prefix = "liburing-liburing-{version}",
        urls = ["https://github.com/axboe/liburing/archive/liburing-{version}.tar.gz"],
    ),
    # This dependency is built only when performance tracing is enabled with the
    # option --define=perf_tracing=enabled. It's never built for releases.
    com_github_google_perfetto = dict(
        version = "53.0",
        sha256 = "b25023f3281165a1a7d7cde9f3ed2dfcfce022ffd727e77f6589951e0ba6af9a",
        strip_prefix = "perfetto-{version}/sdk",
        urls = ["https://github.com/google/perfetto/archive/v{version}.tar.gz"],
    ),
    com_github_cares_cares = dict(
        version = "1.34.6",
        sha256 = "912dd7cc3b3e8a79c52fd7fb9c0f4ecf0aaa73e45efda880266a2d6e26b84ef5",
        strip_prefix = "c-ares-{version}",
        urls = ["https://github.com/c-ares/c-ares/releases/download/v{version}/c-ares-{version}.tar.gz"],
    ),
    libcircllhist = dict(
        version = "0.3.2",
        sha256 = "6dfbd649fde380f7a2256def43b9c6374c6d6fe768178b09e39eedf874b139f4",
        strip_prefix = "libcircllhist-py-{version}",
        urls = ["https://github.com/openhistogram/libcircllhist/archive/refs/tags/py-{version}.tar.gz"],
    ),
    com_github_cyan4973_xxhash = dict(
        version = "0.8.3",
        sha256 = "aae608dfe8213dfd05d909a57718ef82f30722c392344583d3f39050c7f29a80",
        strip_prefix = "xxHash-{version}",
        urls = ["https://github.com/Cyan4973/xxHash/archive/v{version}.tar.gz"],
    ),
    com_github_envoyproxy_sqlparser = dict(
        version = "3b40ba2d106587bdf053a292f7e3bb17e818a57f",
        sha256 = "96c10c8e950a141a32034f19b19cdeb1da48fe859cf96ae5e19f894f36c62c71",
        strip_prefix = "sql-parser-{version}",
        urls = ["https://github.com/envoyproxy/sql-parser/archive/{version}.tar.gz"],
    ),
    com_github_mirror_tclap = dict(
        version = "1.2.5",
        sha256 = "7e87d13734076fa4f626f6144ce9a02717198b3f054341a6886e2107b048b235",
        strip_prefix = "tclap-{version}",
        urls = ["https://github.com/mirror/tclap/archive/v{version}.tar.gz"],
    ),
    com_github_fmtlib_fmt = dict(
        version = "12.1.0",
        sha256 = "695fd197fa5aff8fc67b5f2bbc110490a875cdf7a41686ac8512fb480fa8ada7",
        strip_prefix = "fmt-{version}",
        urls = ["https://github.com/fmtlib/fmt/releases/download/{version}/fmt-{version}.zip"],
    ),
    com_github_gabime_spdlog = dict(
        version = "1.17.0",
        sha256 = "d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744",
        strip_prefix = "spdlog-{version}",
        urls = ["https://github.com/gabime/spdlog/archive/v{version}.tar.gz"],
    ),
    com_github_google_libprotobuf_mutator = dict(
        version = "1.5",
        sha256 = "895958881b4993df70b4f652c2d82c5bd97d22f801ca4f430d6546809df293d5",
        strip_prefix = "libprotobuf-mutator-{version}",
        urls = ["https://github.com/google/libprotobuf-mutator/archive/v{version}.tar.gz"],
    ),
    com_github_google_libsxg = dict(
        version = "beaa3939b76f8644f6833267e9f2462760838f18",
        sha256 = "082bf844047a9aeec0d388283d5edc68bd22bcf4d32eb5a566654ae89956ad1f",
        strip_prefix = "libsxg-{version}",
        urls = ["https://github.com/google/libsxg/archive/{version}.tar.gz"],
    ),
    com_github_google_tcmalloc = dict(
        version = "5da4a882003102fba0c0c0e8f6372567057332eb",
        sha256 = "fd92d64d8302f1677570fdff844e8152c314e559a6c788c6bfc3844954d0dabd",
        strip_prefix = "tcmalloc-{version}",
        urls = ["https://github.com/google/tcmalloc/archive/{version}.tar.gz"],
    ),
    gperftools = dict(
        version = "2.17.2",
        sha256 = "bb172a54312f623b53d8b94cab040248c559decdb87574ed873e80b516e6e8eb",
        strip_prefix = "gperftools-{version}",
        urls = ["https://github.com/gperftools/gperftools/releases/download/gperftools-{version}/gperftools-{version}.tar.gz"],
    ),
    com_github_grpc_grpc = dict(
        version = "1.76.0",
        sha256 = "0af37b800953130b47c075b56683ee60bdc3eda3c37fc6004193f5b569758204",
        strip_prefix = "grpc-{version}",
        urls = ["https://github.com/grpc/grpc/archive/v{version}.tar.gz"],
    ),
    rules_proto_grpc = dict(
        version = "4.6.0",
        sha256 = "2a0860a336ae836b54671cbbe0710eec17c64ef70c4c5a88ccfd47ea6e3739bd",
        strip_prefix = "rules_proto_grpc-{version}",
        urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/releases/download/{version}/rules_proto_grpc-{version}.tar.gz"],
    ),
    com_github_unicode_org_icu = dict(
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
    com_github_intel_ipp_crypto_crypto_mb = dict(
        version = "1.3.0",
        sha256 = "a1d87cb3b90fe4718609e4e9dd8343fd4531bb815e69bad901ac6b46f98b3b53",
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
    com_github_intel_qatlib = dict(
        version = "25.08.0",
        sha256 = "786101683b4817ded72c8ea51204a190aa26e0b5ac8205ee199c7a9438138770",
        strip_prefix = "qatlib-{version}",
        urls = ["https://github.com/intel/qatlib/archive/refs/tags/{version}.tar.gz"],
    ),
    com_github_intel_qatzip = dict(
        version = "1.3.1",
        sha256 = "75e6e57f49da739d0a509220263e9dabb30b1e8c94be11c809aecc0adf4ee2dc",
        strip_prefix = "QATzip-{version}",
        urls = ["https://github.com/intel/QATzip/archive/v{version}.tar.gz"],
    ),
    com_github_qat_zstd = dict(
        version = "1.0.0",
        sha256 = "00f2611719f0a1c9585965c6c3c1fe599119aa8e932a569041b1876ffc944fb3",
        strip_prefix = "QAT-ZSTD-Plugin-{version}",
        urls = ["https://github.com/intel/QAT-ZSTD-Plugin/archive/refs/tags/v{version}.tar.gz"],
    ),
    com_github_luajit_luajit = dict(
        # LuaJIT only provides rolling releases
        version = "871db2c84ecefd70a850e03a6c340214a81739f0",
        sha256 = "ab3f16d82df6946543565cfb0d2810d387d79a3a43e0431695b03466188e2680",
        strip_prefix = "LuaJIT-{version}",
        urls = ["https://github.com/LuaJIT/LuaJIT/archive/{version}.tar.gz"],
    ),
    com_github_nghttp2_nghttp2 = dict(
        version = "1.66.0",
        sha256 = "e178687730c207f3a659730096df192b52d3752786c068b8e5ee7aeb8edae05a",
        strip_prefix = "nghttp2-{version}",
        urls = ["https://github.com/nghttp2/nghttp2/releases/download/v{version}/nghttp2-{version}.tar.gz"],
    ),
    io_hyperscan = dict(
        version = "5.4.2",
        sha256 = "32b0f24b3113bbc46b6bfaa05cf7cf45840b6b59333d078cc1f624e4c40b2b99",
        strip_prefix = "hyperscan-{version}",
        urls = ["https://github.com/intel/hyperscan/archive/v{version}.tar.gz"],
    ),
    io_vectorscan = dict(
        version = "5.4.11",
        sha256 = "905f76ad1fa9e4ae0eb28232cac98afdb96c479666202c5a4c27871fb30a2711",
        strip_prefix = "vectorscan-vectorscan-{version}",
        urls = ["https://codeload.github.com/VectorCamp/vectorscan/tar.gz/refs/tags/vectorscan/{version}"],
    ),
    io_opentelemetry_cpp = dict(
        version = "1.24.0",
        sha256 = "7b8e966affca1daf1906272f4d983631cad85fb6ea60fb6f55dcd1811a730604",
        strip_prefix = "opentelemetry-cpp-{version}",
        urls = ["https://github.com/open-telemetry/opentelemetry-cpp/archive/refs/tags/v{version}.tar.gz"],
    ),
    skywalking_data_collect_protocol = dict(
        sha256 = "5b7c49eff204c423b3d1ffc3b9ec84f2d77838b30464e4a3d6158cf0b6a8429a",
        urls = ["https://github.com/apache/skywalking-data-collect-protocol/archive/v{version}.tar.gz"],
        strip_prefix = "skywalking-data-collect-protocol-{version}",
        version = "10.3.0",
    ),
    com_github_skyapm_cpp2sky = dict(
        sha256 = "d7e52f517de5a1dc7d927dd63a2e5aa5cf8c2dcfd8fcf6b64e179978daf1c3ed",
        version = "0.6.0",
        strip_prefix = "cpp2sky-{version}",
        urls = ["https://github.com/SkyAPM/cpp2sky/archive/v{version}.tar.gz"],
    ),
    com_github_datadog_dd_trace_cpp = dict(
        version = "2.0.0",
        sha256 = "e4a0dabc3e186ce99c71685178448f93c501577102cdc50ddbf12cbaaba54713",
        strip_prefix = "dd-trace-cpp-{version}",
        urls = ["https://github.com/DataDog/dd-trace-cpp/archive/v{version}.tar.gz"],
    ),
    com_github_google_benchmark = dict(
        version = "1.9.4",
        sha256 = "b334658edd35efcf06a99d9be21e4e93e092bd5f95074c1673d5c8705d95c104",
        strip_prefix = "benchmark-{version}",
        urls = ["https://github.com/google/benchmark/archive/v{version}.tar.gz"],
    ),
    com_github_libevent_libevent = dict(
        # This SHA includes the new "prepare" and "check" watchers, used for event loop performance
        # stats (see https://github.com/libevent/libevent/pull/793) and the fix for a race condition
        # in the watchers (see https://github.com/libevent/libevent/pull/802).
        # This also includes the fixes for https://github.com/libevent/libevent/issues/806
        # and https://github.com/envoyproxy/envoy-mobile/issues/215.
        # This also includes the fixes for Phantom events with EV_ET (see
        # https://github.com/libevent/libevent/issues/984).
        # This also includes the wepoll backend for Windows (see
        # https://github.com/libevent/libevent/pull/1006)
        # TODO(adip): Update to v2.2 when it is released.
        version = "62c152d9a7cd264b993dad730c4163c6ede2e0a3",
        sha256 = "4c80e5fe044ce5f8055b20a2f141ee32ec2614000f3e95d2aa81611a4c8f5213",
        strip_prefix = "libevent-{version}",
        urls = ["https://github.com/libevent/libevent/archive/{version}.tar.gz"],
    ),
    net_colm_open_source_colm = dict(
        # The latest release version v0.14.7 prevents building statically (see
        # https://github.com/adrian-thurston/colm/issues/146). The latest SHA includes the fix (see
        # https://github.com/adrian-thurston/colm/commit/fc61ecb3a22b89864916ec538eaf04840e7dd6b5).
        # TODO(zhxie): Update to the next release version when it is released.
        version = "2d8ba76ddaf6634f285d0a81ee42d5ee77d084cf",
        sha256 = "0399e9bef7603a8f3d94acd0b0af6b5944cc3103e586734719379d3ec09620c0",
        strip_prefix = "colm-{version}",
        urls = ["https://github.com/adrian-thurston/colm/archive/{version}.tar.gz"],
    ),
    net_colm_open_source_ragel = dict(
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
    org_boost = dict(
        version = "1.89.0",
        sha256 = "9de758db755e8330a01d995b0a24d09798048400ac25c03fc5ea9be364b13c93",
        strip_prefix = "boost_{underscore_version}",
        urls = ["https://archives.boost.io/release/{version}/source/boost_{underscore_version}.tar.gz"],
    ),
    org_brotli = dict(
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
    com_github_jbeder_yaml_cpp = dict(
        version = "0.8.0",
        sha256 = "fbe74bbdcee21d656715688706da3c8becfd946d92cd44705cc6098bb23b3a16",
        strip_prefix = "yaml-cpp-{version}",
        urls = ["https://github.com/jbeder/yaml-cpp/archive/{version}.tar.gz"],
        # YAML is also used for runtime as well as controlplane. It shouldn't appear on the
        # dataplane but we can't verify this automatically due to code structure today.
    ),
    com_github_msgpack_cpp = dict(
        version = "7.0.0",
        sha256 = "7504b7af7e7b9002ce529d4f941e1b7fb1fb435768780ce7da4abaac79bb156f",
        strip_prefix = "msgpack-cxx-{version}",
        urls = ["https://github.com/msgpack/msgpack-c/releases/download/cpp-{version}/msgpack-cxx-{version}.tar.gz"],
    ),
    com_github_alibaba_hessian2_codec = dict(
        version = "6f5a64770f0374a761eece13c8863b80dc5adcd8",
        sha256 = "bb4c4af6a7e3031160bf38dfa957b0ee950e2d8de47d4ba14c7a658c3a2c74d1",
        strip_prefix = "hessian2-codec-{version}",
        urls = ["https://github.com/alibaba/hessian2-codec/archive/{version}.tar.gz"],
    ),
    com_github_nlohmann_json = dict(
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
    com_github_ncopa_suexec = dict(
        version = "0.3",
        sha256 = "1de7479857879b6d14772792375290a87eac9a37b0524d39739a4a0739039620",
        strip_prefix = "su-exec-{version}",
        urls = ["https://github.com/ncopa/su-exec/archive/v{version}.tar.gz"],
    ),
    com_google_googletest = dict(
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
        sha256 = "6b6599b54c88d75904b7471f5ca34a725fa0af92e134dd1a32d5b395aa4b4ca8",
        strip_prefix = "protobuf-{version}",
        urls = ["https://github.com/protocolbuffers/protobuf/releases/download/v{version}/protobuf-{version}.tar.gz"],
    ),
    grpc_httpjson_transcoding = dict(
        version = "a6e226f9a2e656a973df3ad48f0ee5efacce1a28",
        sha256 = "45dc1a630f518df21b4e044e32b27c7b02ae77ef401b48a20e5ffde0f070113f",
        strip_prefix = "grpc-httpjson-transcoding-{version}",
        urls = ["https://github.com/grpc-ecosystem/grpc-httpjson-transcoding/archive/{version}.tar.gz"],
    ),
    com_google_protoconverter = dict(
        version = "1db76535b86b80aa97489a1edcc7009e18b67ab7",
        sha256 = "9555d9cf7bd541ea5fdb67d7d6b72ea44da77df3e27b960b4155dc0c6b81d476",
        strip_prefix = "proto-converter-{version}",
        urls = ["https://github.com/grpc-ecosystem/proto-converter/archive/{version}.zip"],
    ),
    com_google_protofieldextraction = dict(
        version = "d5d39f0373e9b6691c32c85929838b1006bcb3fb",
        sha256 = "cba864db90806515afa553aaa2fb3683df2859a7535e53a32cb9619da9cebc59",
        strip_prefix = "proto-field-extraction-{version}",
        urls = ["https://github.com/grpc-ecosystem/proto-field-extraction/archive/{version}.zip"],
    ),
    com_google_protoprocessinglib = dict(
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
        version = "0.59.0",
        sha256 = "68af54cb97fbdee5e5e8fe8d210d15a518f9d62abfd71620c3eaff3b26a5ff86",
        urls = ["https://github.com/bazelbuild/rules_go/releases/download/v{version}/rules_go-v{version}.zip"],
    ),
    rules_cc = dict(
        version = "0.2.16",
        sha256 = "458b658277ba51b4730ea7a2020efdf1c6dcadf7d30de72e37f4308277fa8c01",
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
        version = "7.12.5",
        sha256 = "17b18cb4f92ab7b94aa343ce78531b73960b1bed2ba166e5b02c9fdf0b0ac270",
        urls = ["https://github.com/bazelbuild/rules_java/releases/download/{version}/rules_java-{version}.tar.gz"],
    ),
    rules_python = dict(
        version = "1.7.0",
        sha256 = "f609f341d6e9090b981b3f45324d05a819fd7a5a56434f849c761971ce2c47da",
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
        version = "0.6.1",
        sha256 = "e6b87c89bd0b27039e3af2c5da01147452f240f75d505f5b6880874f31036307",
        strip_prefix = "rules_shell-{version}",
        urls = ["https://github.com/bazelbuild/rules_shell/releases/download/v{version}/rules_shell-v{version}.tar.gz"],
    ),
    com_github_wamr = dict(
        version = "WAMR-2.4.4",
        sha256 = "03ad51037f06235577b765ee042a462326d8919300107af4546719c35525b298",
        strip_prefix = "wasm-micro-runtime-{version}",
        urls = ["https://github.com/bytecodealliance/wasm-micro-runtime/archive/{version}.tar.gz"],
    ),
    com_github_wasmtime = dict(
        version = "24.0.4",
        sha256 = "d714d987a50cfc7d0b384ef4720e7c757cf4f5b7df617cbf38e432a3dc6c400d",
        strip_prefix = "wasmtime-{version}",
        urls = ["https://github.com/bytecodealliance/wasmtime/archive/v{version}.tar.gz"],
    ),
    v8 = dict(
        # NOTE: Update together with proxy_wasm_cpp_host, highway, fast_float, dragonbox, simdutf, and fp16.
        # Patch contains workaround for https://github.com/bazelbuild/rules_python/issues/1221
        version = "13.8.258.26",
        # Follow this guide to pick next stable release: https://v8.dev/docs/version-numbers#which-v8-version-should-i-use%3F
        strip_prefix = "v8-{version}",
        sha256 = "4ffc27074d3f79e8e6401e390443dcf02755349002be4a1b01e72a3cd9457d15",
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
        version = "0a92994d729ff76a58f692d3028ca1b64b145d91",
        strip_prefix = "FP16-{version}",
        sha256 = "e66e65515fa09927b348d3d584c68be4215cfe664100d01c9dbc7655a5716d70",
        urls = ["https://github.com/Maratyszcza/FP16/archive/{version}.zip"],
    ),
    simdutf = dict(
        # NOTE: Update together with v8 and proxy_wasm_cpp_host.
        version = "7.3.4",
        sha256 = "a8d2b481a2089280b84df7dc234223b658056b5bbd40bd4d476902d25d353a1f",
        urls = ["https://github.com/simdutf/simdutf/releases/download/v{version}/singleheader.zip"],
    ),
    com_github_google_quiche = dict(
        version = "b7b4c0cfe393a57b8706b0f1be81518595daaa44",
        sha256 = "9d8344faf932165b6013f8fdd2cbfe2be7c2e7a5129c5e572036d13718a3f1bf",
        urls = ["https://github.com/google/quiche/archive/{version}.tar.gz"],
        strip_prefix = "quiche-{version}",
    ),
    googleurl = dict(
        version = "dd4080fec0b443296c0ed0036e1e776df8813aa7",
        sha256 = "4ffa45a827646692e7b26e2a8c0dcbc1b1763a26def2fbbd82362970962a2fcf",
        urls = ["https://github.com/google/gurl/archive/{version}.tar.gz"],
        strip_prefix = "gurl-{version}",
    ),
    com_google_cel_spec = dict(
        version = "0.25.1",
        sha256 = "13583c5a312861648449845b709722676a3c9b43396b6b8e9cbe4538feb74ad2",
        strip_prefix = "cel-spec-{version}",
        urls = ["https://github.com/google/cel-spec/archive/v{version}.tar.gz"],
    ),
    com_google_cel_cpp = dict(
        version = "0.14.0",
        sha256 = "0a4f9a1c0bcc83629eb30d1c278883d32dec0f701efcdabd7bebf33fef8dab71",
        strip_prefix = "cel-cpp-{version}",
        urls = ["https://github.com/google/cel-cpp/archive/v{version}.tar.gz"],
    ),
    com_github_google_flatbuffers = dict(
        version = "25.12.19",
        sha256 = "f81c3162b1046fe8b84b9a0dbdd383e24fdbcf88583b9cb6028f90d04d90696a",
        strip_prefix = "flatbuffers-{version}",
        urls = ["https://github.com/google/flatbuffers/archive/v{version}.tar.gz"],
    ),
    com_googlesource_code_re2 = dict(
        version = "2024-07-02",
        sha256 = "a835fe55fbdcd8e80f38584ab22d0840662c67f2feb36bd679402da9641dc71e",
        strip_prefix = "re2-{version}",
        urls = ["https://github.com/google/re2/releases/download/{version}/re2-{version}.zip"],
    ),
    kafka_source = dict(
        version = "3.9.1",
        sha256 = "c15b82940cfb9f67fce909d8600dc8bcfc42d2795da2c26c149d03a627f85234",
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
        version = "beb8a4ece9eede4ab21d89d723359607600296d4",
        sha256 = "dbd2d449fca10c1cd655efe21eee34fe880f4ff5fe7f01562f7829ca8dd4b2bf",
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
        version = "1.0.0",
        sha256 = "852b71bfa15712cec124e4a57179b6bc95d59fdf5052945f5d550e072501a769",
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
        version = "0.68.1",
        sha256 = "c8aa806cf6066679ac23463241ee80ad692265dad0465f51111cbbe30b890352",
        # Note: rules_rust should point to the releases, not archive to avoid the hassle of bootstrapping in crate_universe.
        # This is described in https://bazelbuild.github.io/rules_rust/crate_universe.html#setup, otherwise bootstrap
        # is required which in turn requires a system CC toolchains, not the bazel controlled ones.
        urls = ["https://github.com/bazelbuild/rules_rust/releases/download/{version}/rules_rust-{version}.tar.gz"],
    ),
    com_github_fdio_vpp_vcl = dict(
        version = "85abefb55ee931fa4e45c0b6a9fc8c43118651b3",
        sha256 = "5624c4a4407285d9f269d0041ed4fc8d5fa3664abc22850069236b026f97d3f2",
        strip_prefix = "vpp-{version}",
        urls = ["https://github.com/FDio/vpp/archive/{version}.tar.gz"],
    ),
    intel_dlb = dict(
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
    com_github_maxmind_libmaxminddb = dict(
        version = "1.12.2",
        sha256 = "1bfbf8efba3ed6462e04e225906ad5ce5fe958aa3d626a1235b2a2253d600743",
        strip_prefix = "libmaxminddb-{version}",
        urls = ["https://github.com/maxmind/libmaxminddb/releases/download/{version}/libmaxminddb-{version}.tar.gz"],
    ),
    thrift = dict(
        version = "0.22.0",
        sha256 = "c4649c5879dd56c88f1e7a1c03e0fbfcc3b2a2872fb81616bffba5aa8a225a37",
        strip_prefix = "thrift-{version}/lib/py/",
        urls = ["https://github.com/apache/thrift/archive/refs/tags/v{version}.tar.gz"],
    ),
    toolchains_llvm = dict(
        version = "1.6.0",
        sha256 = "2b298a1d7ea99679f5edf8af09367363e64cb9fbc46e0b7c1b1ba2b1b1b51058",
        strip_prefix = "toolchains_llvm-v{version}",
        urls = ["https://github.com/bazel-contrib/toolchains_llvm/releases/download/v{version}/toolchains_llvm-v{version}.tar.gz"],
    ),
    com_github_lz4_lz4 = dict(
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
        version = "4.2.3",
        sha256 = "5bb505d5e0cca0480a330f7f27ccf52c2b8b5214c5bba97df08899f5ef650c23",
        strip_prefix = "cmake-{version}-linux-x86_64",
        urls = ["https://github.com/Kitware/CMake/releases/download/v{version}/cmake-{version}-linux-x86_64.tar.gz"],
    ),
    fips_cmake_linux_aarch64 = dict(
        version = "4.2.3",
        sha256 = "e529c75f18f27ba27c52b329efe7b1f98dc32ccc0c6d193c7ab343f888962672",
        strip_prefix = "cmake-{version}-linux-aarch64",
        urls = ["https://github.com/Kitware/CMake/releases/download/v{version}/cmake-{version}-linux-aarch64.tar.gz"],
    ),
    fips_go_linux_amd64 = dict(
        version = "1.24.4",
        sha256 = "77e5da33bb72aeaef1ba4418b6fe511bc4d041873cbf82e5aa6318740df98717",
        strip_prefix = "go",
        urls = ["https://dl.google.com/go/go{version}.linux-amd64.tar.gz"],
    ),
    fips_go_linux_arm64 = dict(
        version = "1.24.4",
        sha256 = "d5501ee5aca0f258d5fe9bfaed401958445014495dc115f202d43d5210b45241",
        strip_prefix = "go",
        urls = ["https://dl.google.com/go/go{version}.linux-arm64.tar.gz"],
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

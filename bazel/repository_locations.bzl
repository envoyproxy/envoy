REPOSITORY_LOCATIONS = dict(
    boringssl = dict(
        # Use commits from branch "chromium-stable-with-bazel"
        commit = "ab36a84b91b3116bacc85973995504818748d8a9",  # chromium-69.0.3497.81
        remote = "https://github.com/google/boringssl",
    ),
    com_google_absl = dict(
        commit = "92e07e5590752d6b8e67f7f2f86c6286561e8cea",  # 2018-08-01
        remote = "https://github.com/abseil/abseil-cpp",
    ),
    com_github_apache_thrift = dict(
        sha256 = "7d59ac4fdcb2c58037ebd4a9da5f9a49e3e034bf75b3f26d9fe48ba3d8806e6b",
        strip_prefix = "thrift-0.11.0",
        urls = ["https://files.pythonhosted.org/packages/c6/b4/510617906f8e0c5660e7d96fbc5585113f83ad547a3989b80297ac72a74c/thrift-0.11.0.tar.gz"],
    ),
    com_github_bombela_backward = dict(
        sha256 = "ad73be31c5cfcbffbde7d34dba18158a42043a109e7f41946f0b0abd589ed55e",
        strip_prefix = "backward-cpp-1.4",
        urls = ["https://github.com/bombela/backward-cpp/archive/v1.4.tar.gz"],
    ),
    com_github_circonus_labs_libcircllhist = dict(
        commit = "050da53a44dede7bda136b93a9aeef47bd91fa12",  # 2018-07-02
        remote = "https://github.com/circonus-labs/libcircllhist",
    ),
    com_github_cyan4973_xxhash = dict(
        sha256 = "19030315f4fc1b4b2cdb9d7a317069a109f90e39d1fe4c9159b7aaa39030eb95",
        strip_prefix = "xxHash-0.6.5",
        urls = ["https://github.com/Cyan4973/xxHash/archive/v0.6.5.tar.gz"],
    ),
    com_github_eile_tclap = dict(
        sha256 = "f0ede0721dddbb5eba3a47385a6e8681b14f155e1129dd39d1a959411935098f",
        strip_prefix = "tclap-tclap-1-2-1-release-final",
        urls = ["https://github.com/eile/tclap/archive/tclap-1-2-1-release-final.tar.gz"],
    ),
    com_github_fmtlib_fmt = dict(
        sha256 = "46628a2f068d0e33c716be0ed9dcae4370242df135aed663a180b9fd8e36733d",
        strip_prefix = "fmt-4.1.0",
        urls = ["https://github.com/fmtlib/fmt/archive/4.1.0.tar.gz"],
    ),
    com_github_gabime_spdlog = dict(
        sha256 = "94f74fd1b3344733d1db3de2ec22e6cbeb769f93a8baa0d4a22b1f62dc7369f8",
        strip_prefix = "spdlog-0.17.0",
        urls = ["https://github.com/gabime/spdlog/archive/v0.17.0.tar.gz"],
    ),
    com_github_gcovr_gcovr = dict(
        sha256 = "8a60ba6242d67a58320e9e16630d80448ef6d5284fda5fb3eff927b63c8b04a2",
        strip_prefix = "gcovr-3.3",
        urls = ["https://github.com/gcovr/gcovr/archive/3.3.tar.gz"],
    ),
    com_github_google_libprotobuf_mutator = dict(
        commit = "c3d2faf04a1070b0b852b0efdef81e1a81ba925e",  # 2018-03-06
        remote = "https://github.com/google/libprotobuf-mutator",
    ),
    com_github_grpc_grpc = dict(
        sha256 = "c747e4d903f7dcf803be53abed4e4efc5d3e96f6c274ed1dfca7a03fa6f4e36b",
        strip_prefix = "grpc-1.14.2",
        urls = ["https://github.com/grpc/grpc/archive/v1.14.2.tar.gz"],
    ),
    com_github_nanopb_nanopb = dict(
        # From: https://github.com/grpc/grpc/blob/v1.14.0/bazel/grpc_deps.bzl#L123
        sha256 = "8bbbb1e78d4ddb0a1919276924ab10d11b631df48b657d960e0c795a25515735",
        strip_prefix = "nanopb-f8ac463766281625ad710900479130c7fcb4d63b",
        urls = ["https://github.com/nanopb/nanopb/archive/f8ac463766281625ad710900479130c7fcb4d63b.tar.gz"],
    ),
    io_opentracing_cpp = dict(
        sha256 = "4455ca507936bc4b658ded10a90d8ebbbd61c58f06207be565a4ffdc885687b5",
        strip_prefix = "opentracing-cpp-1.5.0",
        urls = ["https://github.com/opentracing/opentracing-cpp/archive/v1.5.0.tar.gz"],
    ),
    com_lightstep_tracer_cpp = dict(
        sha256 = "defbf471facfebde6523ca1177529b63784893662d4ef2c60db074be8aef0634",
        strip_prefix = "lightstep-tracer-cpp-0.8.0",
        urls = ["https://github.com/lightstep/lightstep-tracer-cpp/archive/v0.8.0.tar.gz"],
    ),
    lightstep_vendored_googleapis = dict(
        # From: https://github.com/lightstep/lightstep-tracer-cpp/blob/v0.8.0/lightstep-tracer-common/third_party/googleapis/README.lightstep-tracer-common#L6
        commit = "d6f78d948c53f3b400bb46996eb3084359914f9b",
        remote = "https://github.com/google/googleapis",
    ),
    com_github_google_jwt_verify = dict(
        commit = "4eb9e96485b71e00d43acc7207501caafb085b4a",  # 2018-06-11
        remote = "https://github.com/google/jwt_verify_lib",
    ),
    com_github_nodejs_http_parser = dict(
        # 2018-07-20 snapshot to pick up:
        # A performance fix, nodejs/http-parser PR 422.
        # A bug fix, nodejs/http-parser PR 432.
        # TODO(brian-pane): Upgrade to the next http-parser release once it's available
        commit = "77310eeb839c4251c07184a5db8885a572a08352",
        remote = "https://github.com/nodejs/http-parser",
    ),
    com_github_pallets_jinja = dict(
        sha256 = "0d31d3466c313a9ca014a2d904fed18cdac873a5ba1f7b70b8fd8b206cd860d6",
        strip_prefix = "jinja-2.10",
        urls = ["https://github.com/pallets/jinja/archive/2.10.tar.gz"],
    ),
    com_github_pallets_markupsafe = dict(
        sha256 = "dc3938045d9407a73cf9fdd709e2b1defd0588d50ffc85eb0786c095ec846f15",
        strip_prefix = "markupsafe-1.0",
        urls = ["https://github.com/pallets/markupsafe/archive/1.0.tar.gz"],
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
        sha256 = "9bf1fe5182a604b4135edc1a425ae356c9ad15e9b23f9f12a02e80184c3a249c",
        strip_prefix = "googletest-release-1.8.1",
        urls = ["https://github.com/google/googletest/archive/release-1.8.1.tar.gz"],
    ),
    com_google_protobuf = dict(
        # TODO(htuch): Switch back to released versions for protobuf when a release > 3.6.0 happens
        # that includes:
        # - https://github.com/google/protobuf/commit/f35669b8d3f46f7f1236bd21f14d744bba251e60
        # - https://github.com/google/protobuf/commit/6a4fec616ec4b20f54d5fb530808b855cb664390
        commit = "6a4fec616ec4b20f54d5fb530808b855cb664390",
        remote = "https://github.com/google/protobuf",
    ),
    grpc_httpjson_transcoding = dict(
        commit = "05a15e4ecd0244a981fdf0348a76658def62fa9c",  # 2018-05-30
        remote = "https://github.com/grpc-ecosystem/grpc-httpjson-transcoding",
    ),
    io_bazel_rules_go = dict(
        sha256 = "5a89e9c1fe89d45c4a45132fae94bf130842ce3c77e3a735a68f6991d4e00a25",
        strip_prefix = "rules_go-0.11.2",
        urls = ["https://github.com/bazelbuild/rules_go/archive/0.11.2.tar.gz"],
    ),
    six_archive = dict(
        sha256 = "105f8d68616f8248e24bf0e9372ef04d3cc10104f1980f54d57b2ce73a5ad56a",
        strip_prefix = "",
        urls = ["https://pypi.python.org/packages/source/s/six/six-1.10.0.tar.gz#md5=34eed507548117b2ab523ab14b2f8b55"],
    ),
    # I'd love to name this `com_github_google_subpar`, but something in the Subpar
    # code assumes its repository name is just `subpar`.
    subpar = dict(
        sha256 = "eddbfc920e9cd565500370114316757848b672deba06dc2336acfa81b4ac0e6d",
        strip_prefix = "subpar-1.3.0",
        urls = ["https://github.com/google/subpar/archive/1.3.0.tar.gz"],
    ),
)

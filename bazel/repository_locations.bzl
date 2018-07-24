REPOSITORY_LOCATIONS = dict(
    boringssl = dict(
        # Use commits from branch "chromium-stable-with-bazel"
        commit = "2a52ce799382c87cd3119f3b44fbbebf97061ab6",  # chromium-67.0.3396.62
        remote = "https://github.com/google/boringssl",
    ),
    com_google_absl = dict(
        commit = "92020a042c0cd46979db9f6f0cb32783dc07765e",  # 2018-06-08
        remote = "https://github.com/abseil/abseil-cpp",
    ),
    com_github_apache_thrift = dict(
        sha256 = "7d59ac4fdcb2c58037ebd4a9da5f9a49e3e034bf75b3f26d9fe48ba3d8806e6b",
        urls = ["https://files.pythonhosted.org/packages/c6/b4/510617906f8e0c5660e7d96fbc5585113f83ad547a3989b80297ac72a74c/thrift-0.11.0.tar.gz"],  # 0.11.0
        strip_prefix = "thrift-0.11.0",
    ),
    com_github_bombela_backward = dict(
        commit = "44ae9609e860e3428cd057f7052e505b4819eb84",  # 2018-02-06
        remote = "https://github.com/bombela/backward-cpp",
    ),
    com_github_circonus_labs_libcircllhist = dict(
        commit = "050da53a44dede7bda136b93a9aeef47bd91fa12",  # 2018-07-02
        remote = "https://github.com/circonus-labs/libcircllhist",
    ),
    com_github_cyan4973_xxhash = dict(
        commit = "7cc9639699f64b750c0b82333dced9ea77e8436e",  # v0.6.5
        remote = "https://github.com/Cyan4973/xxHash",
    ),
    com_github_eile_tclap = dict(
        commit = "3627d9402e529770df9b0edf2aa8c0e0d6c6bb41",  # tclap-1-2-1-release-final
        remote = "https://github.com/eile/tclap",
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
        commit = "c0d77201039c7b119b18bc7fb991564c602dd75d",
        remote = "https://github.com/gcovr/gcovr",
    ),
    com_github_google_libprotobuf_mutator = dict(
        commit = "c3d2faf04a1070b0b852b0efdef81e1a81ba925e",
        remote = "https://github.com/google/libprotobuf-mutator",
    ),
    com_github_grpc_grpc = dict(
        commit = "bec3b5ada2c5e5d782dff0b7b5018df646b65cb0",  # v1.12.0
        remote = "https://github.com/grpc/grpc.git",
    ),
    io_opentracing_cpp = dict(
        commit = "3b36b084a4d7fffc196eac83203cf24dfb8696b3",  # v1.4.2
        remote = "https://github.com/opentracing/opentracing-cpp",
    ),
    com_lightstep_tracer_cpp = dict(
        commit = "ae6a6bba65f8c4d438a6a3ac855751ca8f52e1dc",
        remote = "https://github.com/lightstep/lightstep-tracer-cpp",  # v0.7.1
    ),
    lightstep_vendored_googleapis = dict(
        commit = "d6f78d948c53f3b400bb46996eb3084359914f9b",
        remote = "https://github.com/google/googleapis",
    ),
    com_github_google_jwt_verify = dict(
        commit = "4eb9e96485b71e00d43acc7207501caafb085b4a",
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
        commit = "78d2f672149e5b9b7d539c575d2c1bfc12db67a9",  # 2.10
        remote = "https://github.com/pallets/jinja",
    ),
    com_github_pallets_markupsafe = dict(
        commit = "d2a40c41dd1930345628ea9412d97e159f828157",  # 1.0
        remote = "https://github.com/pallets/markupsafe",
    ),
    com_github_tencent_rapidjson = dict(
        commit = "f54b0e47a08782a6131cc3d60f94d038fa6e0a51",  # v1.1.0
        remote = "https://github.com/tencent/rapidjson",
    ),
    com_github_twitter_common_lang = dict(
        sha256 = "56d1d266fd4767941d11c27061a57bc1266a3342e551bde3780f9e9eb5ad0ed1",
        urls = ["https://files.pythonhosted.org/packages/08/bc/d6409a813a9dccd4920a6262eb6e5889e90381453a5f58938ba4cf1d9420/twitter.common.lang-0.3.9.tar.gz"],  # 0.3.9
        strip_prefix = "twitter.common.lang-0.3.9/src",
    ),
    com_github_twitter_common_rpc = dict(
        sha256 = "0792b63fb2fb32d970c2e9a409d3d00633190a22eb185145fe3d9067fdaa4514",
        urls = ["https://files.pythonhosted.org/packages/be/97/f5f701b703d0f25fbf148992cd58d55b4d08d3db785aad209255ee67e2d0/twitter.common.rpc-0.3.9.tar.gz"],  # 0.3.9
        strip_prefix = "twitter.common.rpc-0.3.9/src",
    ),
    com_github_twitter_common_finagle_thrift = dict(
        sha256 = "1e3a57d11f94f58745e6b83348ecd4fa74194618704f45444a15bc391fde497a",
        urls = ["https://files.pythonhosted.org/packages/f9/e7/4f80d582578f8489226370762d2cf6bc9381175d1929eba1754e03f70708/twitter.common.finagle-thrift-0.3.9.tar.gz"],  # 0.3.9
        strip_prefix = "twitter.common.finagle-thrift-0.3.9/src",
    ),
    com_google_googletest = dict(
        commit = "43863938377a9ea1399c0596269e0890b5c5515a",
        remote = "https://github.com/google/googletest",
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
        commit = "0.11.1",
        remote = "https://github.com/bazelbuild/rules_go",
    ),
    six_archive = dict(
        sha256 = "105f8d68616f8248e24bf0e9372ef04d3cc10104f1980f54d57b2ce73a5ad56a",
        strip_prefix = "",
        urls = ["https://pypi.python.org/packages/source/s/six/six-1.10.0.tar.gz#md5=34eed507548117b2ab523ab14b2f8b55"],
    ),
    # I'd love to name this `com_github_google_subpar`, but something in the Subpar
    # code assumes its repository name is just `subpar`.
    subpar = dict(
        commit = "eb23aa7a5361cabc02464476dd080389340a5522",  # HEAD
        remote = "https://github.com/google/subpar",
    ),
)

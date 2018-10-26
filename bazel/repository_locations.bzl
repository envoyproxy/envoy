REPOSITORY_LOCATIONS = dict(
    bazel_gazelle = dict(
        sha256 = "1b959bd6b6ce88fc3fdfc28946adf1eafb1d5e4d470d2e08a51774d09078d031",
        strip_prefix = "bazel-gazelle-0.14.0",
        urls = ["https://github.com/bazelbuild/bazel-gazelle/archive/0.14.0.tar.gz"],
    ),
    boringssl = dict(
        # Use commits from branch "chromium-stable-with-bazel"
        sha256 = "d1700e0455f5f918f8a85ff3ce6cd684d05c766200ba6bdb18c77d5dcadc05a1",
        strip_prefix = "boringssl-060e9a583976e73d1ea8b2bfe8b9cab33c62fa17",
        # chromium-70.0.3538.67
        urls = ["https://github.com/google/boringssl/archive/060e9a583976e73d1ea8b2bfe8b9cab33c62fa17.tar.gz"],
    ),
    com_google_absl = dict(
        sha256 = "3f24d99cfa1b719ed51e12dae7741e4500a703e2c34bede452b457492d1fe36e",
        strip_prefix = "abseil-cpp-92e07e5590752d6b8e67f7f2f86c6286561e8cea",
        # 2018-08-01
        urls = ["https://github.com/abseil/abseil-cpp/archive/92e07e5590752d6b8e67f7f2f86c6286561e8cea.tar.gz"],
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
        sha256 = "9949e2864b8ad00ee5c3e9c1c3c01e51b6b68bb442a919652fc66b9776477987",
        strip_prefix = "libcircllhist-fd8a14463739d247b414825cc56ca3946792a3b9",
        # 2018-07-02
        urls = ["https://github.com/circonus-labs/libcircllhist/archive/fd8a14463739d247b414825cc56ca3946792a3b9.tar.gz"],
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
        sha256 = "3c812a18e9f72a88631ab4732a97ce9ef5bcbefb3235e9fd465f059ba204359b",
        strip_prefix = "fmt-5.2.1",
        urls = ["https://github.com/fmtlib/fmt/archive/5.2.1.tar.gz"],
    ),
    com_github_gabime_spdlog = dict(
        sha256 = "867a4b7cedf9805e6f76d3ca41889679054f7e5a3b67722fe6d0eae41852a767",
        strip_prefix = "spdlog-1.2.1",
        urls = ["https://github.com/gabime/spdlog/archive/v1.2.1.tar.gz"],
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
    com_github_grpc_grpc = dict(
        sha256 = "013cc34f3c51c0f87e059a12ea203087a7a15dca2e453295345e1d02e2b9634b",
        strip_prefix = "grpc-1.15.0",
        urls = ["https://github.com/grpc/grpc/archive/v1.15.0.tar.gz"],
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
        sha256 = "d1ef4f790eeaa805e7b364de05b91f9eed66bd6ae46f1483bbf49c33d86998e5",
        strip_prefix = "googleapis-d6f78d948c53f3b400bb46996eb3084359914f9b",
        # From: https://github.com/lightstep/lightstep-tracer-cpp/blob/v0.8.0/lightstep-tracer-common/third_party/googleapis/README.lightstep-tracer-common#L6
        urls = ["https://github.com/googleapis/googleapis/archive/d6f78d948c53f3b400bb46996eb3084359914f9b.tar.gz"],
    ),
    com_github_google_jwt_verify = dict(
        sha256 = "499f1e145c19f33031eb8fc6452d5d391b4cecfdeda23e2055386a3b33be4d41",
        strip_prefix = "jwt_verify_lib-66792a057ec54e4b75c6a2eeda4e98220bd12a9a",
        # 2018-08-17
        urls = ["https://github.com/google/jwt_verify_lib/archive/66792a057ec54e4b75c6a2eeda4e98220bd12a9a.tar.gz"],
    ),
    com_github_nodejs_http_parser = dict(
        sha256 = "f742dc5a206958c4d0a6b2c35e3e102afb5683f55f7a7cb1eae024a03f081347",
        strip_prefix = "http-parser-77310eeb839c4251c07184a5db8885a572a08352",
        # 2018-07-20 snapshot to pick up:
        # A performance fix, nodejs/http-parser PR 422.
        # A bug fix, nodejs/http-parser PR 432.
        # TODO(brian-pane): Upgrade to the next http-parser release once it's available
        urls = ["https://github.com/nodejs/http-parser/archive/77310eeb839c4251c07184a5db8885a572a08352.tar.gz"],
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
        sha256 = "3d610ac90f8fa16e12490088605c248b85fdaf23114ce4b3605cdf81f7823604",
        strip_prefix = "protobuf-fa252ec2a54acb24ddc87d48fed1ecfd458445fd",
        # TODO(htuch): Switch back to released versions for protobuf when a release > 3.6.0 happens
        # that includes:
        # - https://github.com/google/protobuf/commit/f35669b8d3f46f7f1236bd21f14d744bba251e60
        # - https://github.com/google/protobuf/commit/6a4fec616ec4b20f54d5fb530808b855cb664390
        # - https://github.com/google/protobuf/commit/fa252ec2a54acb24ddc87d48fed1ecfd458445fd
        urls = ["https://github.com/protocolbuffers/protobuf/archive/fa252ec2a54acb24ddc87d48fed1ecfd458445fd.tar.gz"],
    ),
    grpc_httpjson_transcoding = dict(
        sha256 = "9765764644d74af9a9654f7fb90cf2bc7228014664668719a589a4677967ca09",
        strip_prefix = "grpc-httpjson-transcoding-05a15e4ecd0244a981fdf0348a76658def62fa9c",
        # 2018-05-30
        urls = ["https://github.com/grpc-ecosystem/grpc-httpjson-transcoding/archive/05a15e4ecd0244a981fdf0348a76658def62fa9c.tar.gz"],
    ),
    com_github_golang_protobuf = dict(
        # TODO(sesmith177): Remove this dependency when both:
        #   1. There's a release of golang/protobuf that includes
        #      https://github.com/golang/protobuf/commit/31e0d063dd98c052257e5b69eeb006818133f45c
        #   2. That release is included in rules_go
        sha256 = "4cbd5303a5cf85791b3c310a50a479027c035d75091bb90c482ba67b0a2cf5b4",
        strip_prefix = "protobuf-31e0d063dd98c052257e5b69eeb006818133f45c",
        urls = ["https://github.com/golang/protobuf/archive/31e0d063dd98c052257e5b69eeb006818133f45c.tar.gz"],
    ),
    io_bazel_rules_go = dict(
        sha256 = "ee5fe78fe417c685ecb77a0a725dc9f6040ae5beb44a0ba4ddb55453aad23a8a",
        urls = ["https://github.com/bazelbuild/rules_go/releases/download/0.16.0/rules_go-0.16.0.tar.gz"],
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

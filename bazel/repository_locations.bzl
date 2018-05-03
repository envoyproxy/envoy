REPOSITORY_LOCATIONS = dict(
    boringssl = dict(
        # Use commits from branch "chromium-stable-with-bazel"
        commit = "c386d2b324468137bcbf2e7a1020da7579f7ab48",  # chromium-66.0.3359.117
        remote = "https://github.com/google/boringssl",
    ),
    com_google_absl = dict(
        commit = "787891a3882795cee0364e8a0f0dda315578d155",
        remote = "https://github.com/abseil/abseil-cpp",
    ),
    com_github_bombela_backward = dict(
        commit = "44ae9609e860e3428cd057f7052e505b4819eb84",  # 2018-02-06
        remote = "https://github.com/bombela/backward-cpp",
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
        sha256 = "10a9f184d4d66f135093a08396d3b0a0ebe8d97b79f8b3ddb8559f75fe4fcbc3",
        strip_prefix = "fmt-4.0.0",
        urls = ["https://github.com/fmtlib/fmt/releases/download/4.0.0/fmt-4.0.0.zip"],
    ),
    com_github_gabime_spdlog = dict(
        sha256 = "b88d7be261d9089c817fc8cee6c000d69f349b357828e4c7f66985bc5d5360b8",
        strip_prefix = "spdlog-0.16.3",
        urls = ["https://github.com/gabime/spdlog/archive/v0.16.3.tar.gz"],
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
        commit = "bd44e485f69d70ca4095cea92decd98de3892aa6", # v1.11.0
        remote = "https://github.com/grpc/grpc.git",
    ),
    io_opentracing_cpp = dict(
        commit = "900f9d9297a71ddf4a5dff2051a01493014c07c5", # v1.4.0
        remote = "https://github.com/opentracing/opentracing-cpp",
    ),
    com_lightstep_tracer_cpp = dict(
        commit = "4ea8bda9aed08ad45d6db2a030a1464e8d9b783f",
        remote = "https://github.com/lightstep/lightstep-tracer-cpp", # v0.7.0
    ),
    lightstep_vendored_googleapis = dict(
        commit = "d6f78d948c53f3b400bb46996eb3084359914f9b",
        remote = "https://github.com/google/googleapis",
    ),
    com_github_nodejs_http_parser = dict(
        commit = "54f55a2f02a823e5f5c87abe853bb76d1170718d",  # v2.8.1
        remote = "https://github.com/nodejs/http-parser",
    ),
    com_github_pallets_jinja = dict(
        commit = "d78a1b079cd985eea7d636f79124ab4fc44cb538",  # 2.9.6
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
    com_google_googletest = dict(
        commit = "43863938377a9ea1399c0596269e0890b5c5515a",
        remote = "https://github.com/google/googletest",
    ),
    com_google_protobuf = dict(
        sha256 = "0cc6607e2daa675101e9b7398a436f09167dffb8ca0489b0307ff7260498c13c",
        strip_prefix = "protobuf-3.5.0",
        urls = ["https://github.com/google/protobuf/archive/v3.5.0.tar.gz"],
    ),
    grpc_httpjson_transcoding = dict(
        commit = "e4f58aa07b9002befa493a0a82e10f2e98b51fc6",
        remote = "https://github.com/grpc-ecosystem/grpc-httpjson-transcoding",
    ),
    io_bazel_rules_go = dict(
        commit = "0.10.3",
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

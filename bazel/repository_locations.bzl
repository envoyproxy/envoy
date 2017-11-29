REPOSITORY_LOCATIONS = dict(
    com_google_absl = dict(
        commit = "6de53819a7173bd446156237a37f53464b7732cc",
        remote = "https://github.com/abseil/abseil-cpp",
    ),
    com_github_bombela_backward = dict(
        commit = "cd1c4bd9e48afe812a0e996d335298c455afcd92",  # v1.3
        remote = "https://github.com/bombela/backward-cpp",
    ),
    com_github_cyan4973_xxhash = dict(
        commit = "50a564c33c36b3f0c83f027dd21c25cba2967c72",  # v0.6.3
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
        sha256 = "eb5beb4e53f4bfff5b32eb4db8588484bdc15a17b90eeefef3a9fc74fec1d83d",
        strip_prefix = "spdlog-0.14.0",
        urls = ["https://github.com/gabime/spdlog/archive/v0.14.0.tar.gz"],
    ),
    com_github_gcovr_gcovr = dict(
        commit = "c0d77201039c7b119b18bc7fb991564c602dd75d",
        remote = "https://github.com/gcovr/gcovr",
    ),
    com_github_lightstep_lightstep_tracer_cpp = dict(
        sha256 = "f7477e67eca65f904c0b90a6bfec46d58cccfc998a8e75bc3259b6e93157ff84",
        strip_prefix = "lightstep-tracer-cpp-0.36",
        urls = ["https://github.com/lightstep/lightstep-tracer-cpp/releases/download/v0_36/lightstep-tracer-cpp-0.36.tar.gz"],
    ),
    com_github_nodejs_http_parser = dict(
        commit = "feae95a3a69f111bc1897b9048d9acbc290992f9",  # v2.7.1
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
    envoy_api = dict(
        commit = "334d87c26060602f439976e26a54f26d05a77070",
        remote = "https://github.com/envoyproxy/data-plane-api",
    ),
    grpc_httpjson_transcoding = dict(
        commit = "e4f58aa07b9002befa493a0a82e10f2e98b51fc6",
        remote = "https://github.com/grpc-ecosystem/grpc-httpjson-transcoding",
    ),
    io_bazel_rules_go = dict(
        commit = "4374be38e9a75ff5957c3922adb155d32086fe14",
        remote = "https://github.com/bazelbuild/rules_go",
    ),
    # I'd love to name this `com_github_google_subpar`, but something in the Subpar
    # code assumes its repository name is just `subpar`.
    subpar = dict(
        commit = "eb23aa7a5361cabc02464476dd080389340a5522",  # HEAD
        remote = "https://github.com/google/subpar",
    ),
)

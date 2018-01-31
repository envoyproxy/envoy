REPOSITORY_LOCATIONS = dict(
    com_google_absl = dict(
        commit = "787891a3882795cee0364e8a0f0dda315578d155",
        remote = "https://github.com/abseil/abseil-cpp",
    ),
    com_github_bombela_backward = dict(
        commit = "cd1c4bd9e48afe812a0e996d335298c455afcd92",  # v1.3
        remote = "https://github.com/bombela/backward-cpp",
    ),
    com_github_cyan4973_xxhash = dict(
        commit = "7caf8bd76440c75dfe1070d3acfbd7891aea8fca",  # v0.6.4
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
        sha256 = "2081e5df5e87402398847431e16b87c71dd5c4d632314bb976ace8161f4d32de",
        strip_prefix = "spdlog-0.16.2",
        urls = ["https://github.com/gabime/spdlog/archive/v0.16.2.tar.gz"],
    ),
    com_github_gcovr_gcovr = dict(
        commit = "c0d77201039c7b119b18bc7fb991564c602dd75d",
        remote = "https://github.com/gcovr/gcovr",
    ),
    com_github_grpc_grpc = dict(
        commit = "04ecc18e3a5b8de5bb7ffa20700364ad88dc16f9", # v1.9.0-pre3
        remote = "https://github.com/grpc/grpc.git",
    ),
    io_opentracing_cpp = dict(
        commit = "e57161e2a4bd1f9d3a8d3edf23185f033bb45f17",
        remote = "https://github.com/opentracing/opentracing-cpp", # v1.2.0
    ),
    com_lightstep_tracer_cpp = dict(
        commit = "6a198acd328f976984699f7272bbec7c8b220f65",
        remote = "https://github.com/lightstep/lightstep-tracer-cpp", # v0.6.1
    ),
    lightstep_vendored_googleapis = dict(
        commit = "d6f78d948c53f3b400bb46996eb3084359914f9b",
        remote = "https://github.com/google/googleapis",
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
        commit = "8345af596d78d5da6becb0538fced3d65efbaadf",
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

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
    com_github_bombela_backward = dict(
        commit = "44ae9609e860e3428cd057f7052e505b4819eb84",  # 2018-02-06
        remote = "https://github.com/bombela/backward-cpp",
    ),
    com_github_circonus_labs_libcircllhist = dict(
        commit = "476687ac9cc636fc92ac3070246d757ae6854547",  # 2018-05-08
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
        commit = "bec3b5ada2c5e5d782dff0b7b5018df646b65cb0", # v1.12.0
        remote = "https://github.com/grpc/grpc.git",
    ),
    io_opentracing_cpp = dict(
        commit = "3b36b084a4d7fffc196eac83203cf24dfb8696b3", # v1.4.2
        remote = "https://github.com/opentracing/opentracing-cpp",
    ),
    com_lightstep_tracer_cpp = dict(
        commit = "ae6a6bba65f8c4d438a6a3ac855751ca8f52e1dc",
        remote = "https://github.com/lightstep/lightstep-tracer-cpp", # v0.7.1
    ),
    lightstep_vendored_googleapis = dict(
        commit = "d6f78d948c53f3b400bb46996eb3084359914f9b",
        remote = "https://github.com/google/googleapis",
    ),
    com_github_nodejs_http_parser = dict(
        # 2018-05-30 snapshot to pick up a performance fix, nodejs/http-parser PR 422
        # TODO(brian-pane): Upgrade to the next http-parser release once it's available
        commit = "cf69c8eda9fe79e4682598a7b3d39338dea319a3",
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
    com_google_googletest = dict(
        commit = "43863938377a9ea1399c0596269e0890b5c5515a",
        remote = "https://github.com/google/googletest",
    ),
    com_google_protobuf = dict(
        sha256 = "826425182ee43990731217b917c5c3ea7190cfda141af4869e6d4ad9085a740f",
        strip_prefix = "protobuf-3.5.1",
        urls = ["https://github.com/google/protobuf/archive/v3.5.1.tar.gz"],
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

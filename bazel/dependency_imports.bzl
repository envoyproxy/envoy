load("@aspect_bazel_lib//lib:repositories.bzl", "register_jq_toolchains", "register_yq_toolchains")
load("@base_pip3//:requirements.bzl", pip_dependencies = "install_deps")
load("@bazel_gazelle//:deps.bzl", "gazelle_dependencies", "go_repository")
load("@build_bazel_rules_apple//apple:repositories.bzl", "apple_rules_dependencies")
load("@com_github_aignas_rules_shellcheck//:deps.bzl", "shellcheck_dependencies")
load("@com_github_chrusty_protoc_gen_jsonschema//:deps.bzl", protoc_gen_jsonschema_go_dependencies = "go_dependencies")
load("@com_google_cel_cpp//bazel:deps.bzl", "parser_deps")
load("@dev_pip3//:requirements.bzl", pip_dev_dependencies = "install_deps")
load("@emsdk//:emscripten_deps.bzl", "emscripten_deps")
load("@emsdk//:toolchains.bzl", "register_emscripten_toolchains")
load("@envoy_toolshed//compile:sanitizer_libs.bzl", "setup_sanitizer_libs")
load("@envoy_toolshed//coverage/grcov:grcov_repository.bzl", "grcov_repository")
load("@fuzzing_pip3//:requirements.bzl", pip_fuzzing_dependencies = "install_deps")
load("@io_bazel_rules_go//go:deps.bzl", "go_download_sdk", "go_register_toolchains", "go_rules_dependencies")
load("@proxy_wasm_rust_sdk//bazel:dependencies.bzl", "proxy_wasm_rust_sdk_dependencies")
load("@rules_buf//buf:repositories.bzl", "rules_buf_toolchains")
load("@rules_cc//cc:extensions.bzl", "compatibility_proxy_repo")
load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")
load("@rules_fuzzing//fuzzing:repositories.bzl", "rules_fuzzing_dependencies")
load("@rules_pkg//:deps.bzl", "rules_pkg_dependencies")
load("@rules_proto_grpc//:repositories.bzl", "rules_proto_grpc_toolchains")
load("@rules_rust//crate_universe:defs.bzl", "crates_repository")
load("@rules_rust//crate_universe:repositories.bzl", "crate_universe_dependencies")
load("@rules_rust//rust:defs.bzl", "rust_common")
load("@rules_rust//rust:repositories.bzl", "rules_rust_dependencies", "rust_register_toolchains", "rust_repository_set")

# go version for rules_go
GO_VERSION = "1.24.6"

JQ_VERSION = "1.7"
YQ_VERSION = "4.24.4"

BUF_SHA = "5790beb45aaf51a6d7e68ca2255b22e1b14c9ae405a6c472cdcfc228c66abfc1"
BUF_VERSION = "v1.56.0"

def envoy_dependency_imports(
        go_version = GO_VERSION,
        jq_version = JQ_VERSION,
        yq_version = YQ_VERSION,
        buf_sha = BUF_SHA,
        buf_version = BUF_VERSION):
    compatibility_proxy_repo()
    rules_foreign_cc_dependencies()
    go_rules_dependencies()
    go_register_toolchains(go_version)
    if go_version != "host":
        envoy_download_go_sdks(go_version)
    gazelle_dependencies(go_sdk = "go_sdk")
    apple_rules_dependencies()
    pip_dependencies()
    pip_dev_dependencies()
    pip_fuzzing_dependencies()
    rules_pkg_dependencies()
    emscripten_deps(emscripten_version = "4.0.6")
    register_emscripten_toolchains()

    rust_repository_set(
        name = "rust_linux_s390x",
        exec_triple = "s390x-unknown-linux-gnu",
        extra_target_triples = [
            "wasm32-unknown-unknown",
            "wasm32-wasi",
        ],
        versions = [rust_common.default_version],
    )
    rules_rust_dependencies()
    rust_register_toolchains(
        extra_target_triples = [
            "wasm32-unknown-unknown",
            "wasm32-wasi",
        ],
    )
    crate_universe_dependencies()
    crates_repositories()
    grcov_repository()
    shellcheck_dependencies()
    proxy_wasm_rust_sdk_dependencies()
    rules_fuzzing_dependencies(
        oss_fuzz = True,
        honggfuzz = False,
    )
    register_jq_toolchains(version = jq_version)
    register_yq_toolchains(version = yq_version)
    parser_deps()

    rules_buf_toolchains(
        sha256 = buf_sha,
        version = buf_version,
    )

    setup_sanitizer_libs()

    # These dependencies, like most of the Go in this repository, exist only for the API.
    # These repos also have transient dependencies - `build_external` allows them to use them.
    # TODO(phlax): remove `build_external` and pin all transients
    # Updated to v1.72.0 to match xds/go/go.mod and ensure compatibility with protobuf v1.36.10
    go_repository(
        name = "org_golang_google_grpc",
        build_file_proto_mode = "disable",
        importpath = "google.golang.org/grpc",
        sum = "h1:S7UkcVa60b5AAQTaO6ZKamFp1zMZSU0fGDK2WZLbBnM=",
        version = "v1.72.0",
        build_external = "external",
        # project_url = "https://pkg.go.dev/google.golang.org/grpc",
        # last_update = "2025-04-23"
        # use_category = ["api"],
        # cpe = "cpe:2.3:a:grpc:grpc:*",
    )
    # Updated to v0.35.0 to match xds/go/go.mod
    go_repository(
        name = "org_golang_x_net",
        importpath = "golang.org/x/net",
        sum = "h1:T5GQRQb2y08kTAByq9L4/bz8cipCdA8FbRTXewonqY8=",
        version = "v0.35.0",
        build_external = "external",
        # project_url = "https://pkg.go.dev/golang.org/x/net",
        # last_update = "2025-01-15"
        # use_category = ["api"],
    )
    # Updated to v0.22.0 to match xds/go/go.mod
    go_repository(
        name = "org_golang_x_text",
        importpath = "golang.org/x/text",
        sum = "h1:bofq7m3/HAFvbF51jz3Q9wLg3jkvSPuiZu/pD1XwgtM=",
        version = "v0.22.0",
        build_external = "external",
        # project_url = "https://pkg.go.dev/golang.org/x/text",
        # last_update = "2025-01-15"
        # use_category = ["api"],
    )
    # Updated to match xds/go/go.mod
    go_repository(
        name = "org_golang_google_genproto_googleapis_api",
        importpath = "google.golang.org/genproto/googleapis/api",
        sum = "h1:nwKuGPlUAt+aR+pcrkfFRrTU1BVrSmYyYMxYbUIVHr0=",
        version = "v0.0.0-20250218202821-56aae31c358a",
        build_external = "external",
    )
    # Updated to match xds/go/go.mod
    go_repository(
        name = "org_golang_google_genproto_googleapis_rpc",
        importpath = "google.golang.org/genproto/googleapis/rpc",
        sum = "h1:51aaUVRocpvUOSQKM6Q7VuoaktNIaMCLuhZB6DKksq4=",
        version = "v0.0.0-20250218202821-56aae31c358a",
        build_external = "external",
    )
    go_repository(
        name = "org_golang_google_protobuf",
        importpath = "google.golang.org/protobuf",
        sum = "h1:AYd7cD/uASjIL6Q9LiTjz8JLcrh/88q5UObnmY3aOOE=",
        version = "v1.36.10",
        build_external = "external",
    )
    go_repository(
        name = "com_github_cncf_xds_go",
        importpath = "github.com/cncf/xds/go",
        remote = "https://github.com/mmorel-35/xds",
        vcs = "git",
        commit = "dc55ea33097c5b576d6f39d9b13a9419813a9e76",
        build_external = "external",
    )
    go_repository(
        name = "com_github_spf13_afero",
        importpath = "github.com/spf13/afero",
        sum = "h1:8q6vk3hthlpb2SouZcnBVKboxWQWMDNF38bwholZrJc=",
        version = "v1.3.4",
        build_external = "external",
        # project_url = "https://pkg.go.dev/github.com/spf13/afero",
        # last_update = "2021-03-20"
        # use_category = ["api"],
        # source = "https://github.com/bufbuild/protoc-gen-validate/blob/v0.6.1/dependencies.bzl#L60-L65"
    )
    go_repository(
        name = "com_github_lyft_protoc_gen_star_v2",
        importpath = "github.com/lyft/protoc-gen-star/v2",
        sum = "h1:keaAo8hRuAT0O3DfJ/wM3rufbAjGeJ1lAtWZHDjKGB0=",
        version = "v2.0.1",
        build_external = "external",
        # project_url = "https://pkg.go.dev/github.com/lyft/protoc-gen-star",
        # last_update = "2023-01-06"
        # use_category = ["api"],
        # source = "https://github.com/bufbuild/protoc-gen-validate/blob/v0.10.1/dependencies.bzl#L35-L40"
    )
    go_repository(
        name = "com_github_iancoleman_strcase",
        importpath = "github.com/iancoleman/strcase",
        sum = "h1:ux/56T2xqZO/3cP1I2F86qpeoYPCOzk+KF/UH/Ar+lk=",
        version = "v0.0.0-20180726023541-3605ed457bf7",
        build_external = "external",
        # project_url = "https://pkg.go.dev/github.com/iancoleman/strcase",
        # last_update = "2020-11-22"
        # use_category = ["api"],
        # source = "https://github.com/bufbuild/protoc-gen-validate/blob/v0.6.1/dependencies.bzl#L23-L28"
    )
    go_repository(
        name = "com_github_planetscale_vtprotobuf",
        importpath = "github.com/planetscale/vtprotobuf",
        sum = "h1:ujRGEVWJEoaxQ+8+HMl8YEpGaDAgohgZxJ5S+d2TTFQ=",
        version = "v0.6.1-0.20240409071808-615f978279ca",
        build_external = "external",
    )

    go_repository(
        name = "build_buf_gen_go_bufbuild_protovalidate_protocolbuffers_go",
        importpath = "buf.build/gen/go/bufbuild/protovalidate/protocolbuffers/go",
        sum = "h1:31on4W/yPcV4nZHL4+UCiCvLPsMqe/vJcNg8Rci0scc=",
        version = "v1.36.10-20250912141014-52f32327d4b0.1",
        build_external = "external",
    )

    protoc_gen_jsonschema_go_dependencies()
    rules_proto_grpc_toolchains()

def envoy_download_go_sdks(go_version):
    go_download_sdk(
        name = "go_linux_amd64",
        goos = "linux",
        goarch = "amd64",
        version = go_version,
    )
    go_download_sdk(
        name = "go_linux_arm64",
        goos = "linux",
        goarch = "arm64",
        version = go_version,
    )
    go_download_sdk(
        name = "go_darwin_amd64",
        goos = "darwin",
        goarch = "amd64",
        version = go_version,
    )
    go_download_sdk(
        name = "go_darwin_arm64",
        goos = "darwin",
        goarch = "arm64",
        version = go_version,
    )

def crates_repositories():
    crates_repository(
        name = "dynamic_modules_rust_sdk_crate_index",
        cargo_lockfile = "@envoy//source/extensions/dynamic_modules/sdk/rust:Cargo.lock",
        lockfile = Label("@envoy//source/extensions/dynamic_modules/sdk/rust:Cargo.Bazel.lock"),
        manifests = ["@envoy//source/extensions/dynamic_modules/sdk/rust:Cargo.toml"],
    )

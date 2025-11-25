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

    protoc_gen_jsonschema_go_dependencies()

    # These dependencies, like most of the Go in this repository, exist only for the API.
    # These repos also have transient dependencies - `build_external` allows them to use them.
    # TODO(phlax): remove `build_external` and pin all transients
    go_repository(
        name = "org_golang_google_grpc",
        build_file_proto_mode = "disable",
        importpath = "google.golang.org/grpc",
        sum = "h1:aHQeeJbo8zAkAa3pRzrVjZlbz6uSfeOXlJNQM0RAbz0=",
        version = "v1.68.0",
        build_external = "external",
    )
    go_repository(
        name = "org_golang_x_net",
        importpath = "golang.org/x/net",
        sum = "h1:Mb7Mrk043xzHgnRM88suvJFwzVrRfHEHJEl5/71CKw0=",
        version = "v0.34.0",
        build_external = "external",
    )
    go_repository(
        name = "org_golang_x_text",
        importpath = "golang.org/x/text",
        sum = "h1:zyQAAkrwaneQ066sspRyJaG9VNi/YJ1NfzcGB3hZ/qo=",
        version = "v0.21.0",
        build_external = "external",
    )
    go_repository(
        name = "org_golang_google_genproto_googleapis_api",
        importpath = "google.golang.org/genproto/googleapis/api",
        sum = "h1:+2XxjfsAu6vqFxwGBRcHiMaDCuZiqXGDUDVWVtrFAnE=",
        version = "v0.0.0-20251029180050-ab9386a59fda",
        build_external = "external",
    )
    go_repository(
        name = "org_golang_google_genproto_googleapis_rpc",
        importpath = "google.golang.org/genproto/googleapis/rpc",
        sum = "h1:i/Q+bfisr7gq6feoJnS/DlpdwEL4ihp41fvRiM3Ork0=",
        version = "v0.0.0-20251029180050-ab9386a59fda",
        build_external = "external",
    )
    go_repository(
        name = "org_golang_google_protobuf",
        build_file_proto_mode = "disable",
        importpath = "google.golang.org/protobuf",
        sum = "h1:AYd7cD/uASjIL6Q9LiTjz8JLcrh/88q5UObnmY3aOOE=",
        version = "v1.36.10",
        build_external = "external",
    )
    go_repository(
        name = "com_github_cncf_xds_go",
        importpath = "github.com/cncf/xds/go",
        sum = "h1:gt7U1Igw0xbJdyaCM5H2CnlAlPSkzrhsebQB6WQWjLA=",
        version = "v0.0.0-20251110193048-8bfbf64dc13e",
        build_external = "external",
    )
    go_repository(
        name = "dev_cel_expr",
        importpath = "cel.dev/expr",
        sum = "h1:56OvJKSH3hDGL0ml5uSxZmz3/3Pq4tJ+fb1unVLAFcY=",
        version = "v0.24.0",
        build_external = "external",
    )
    go_repository(
        name = "com_github_spf13_afero",
        importpath = "github.com/spf13/afero",
        sum = "h1:EaGW2JJh15aKOejeuJ+wpFSHnbd7GE6Wvp3TsNhb6LY=",
        version = "v1.10.0",
        build_external = "external",
    )
    go_repository(
        name = "com_github_lyft_protoc_gen_star_v2",
        importpath = "github.com/lyft/protoc-gen-star/v2",
        sum = "h1:sIXJOMrYnQZJu7OB7ANSF4MYri2fTEGIsRLz6LwI4xE=",
        version = "v2.0.4-0.20230330145011-496ad1ac90a4",
        build_external = "external",
    )
    go_repository(
        name = "com_github_iancoleman_strcase",
        importpath = "github.com/iancoleman/strcase",
        sum = "h1:nTXanmYxhfFAMjZL34Ov6gkzEsSJZ5DbhxWjvSASxEI=",
        version = "v0.3.0",
        build_external = "external",
    )
    go_repository(
        name = "com_github_planetscale_vtprotobuf",
        importpath = "github.com/planetscale/vtprotobuf",
        sum = "h1:ujRGEVWJEoaxQ+8+HMl8YEpGaDAgohgZxJ5S+d2TTFQ=",
        version = "v0.6.1-0.20240409071808-615f978279ca",
        build_external = "external",
    )

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

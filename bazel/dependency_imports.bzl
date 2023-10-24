load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")
load("@io_bazel_rules_go//go:deps.bzl", "go_download_sdk", "go_register_toolchains", "go_rules_dependencies")
load("@bazel_gazelle//:deps.bzl", "gazelle_dependencies", "go_repository")
load("@build_bazel_rules_apple//apple:repositories.bzl", "apple_rules_dependencies")
load("@rules_fuzzing//fuzzing:repositories.bzl", "rules_fuzzing_dependencies")
load("@upb//bazel:workspace_deps.bzl", "upb_deps")
load("@rules_pkg//:deps.bzl", "rules_pkg_dependencies")
load("@rules_rust//rust:repositories.bzl", "rules_rust_dependencies", "rust_register_toolchains", "rust_repository_set")
load("@rules_rust//rust:defs.bzl", "rust_common")
load("@proxy_wasm_rust_sdk//bazel:dependencies.bzl", "proxy_wasm_rust_sdk_dependencies")
load("@base_pip3//:requirements.bzl", pip_dependencies = "install_deps")
load("@dev_pip3//:requirements.bzl", pip_dev_dependencies = "install_deps")
load("@fuzzing_pip3//:requirements.bzl", pip_fuzzing_dependencies = "install_deps")
load("@emsdk//:emscripten_deps.bzl", "emscripten_deps")
load("@com_github_aignas_rules_shellcheck//:deps.bzl", "shellcheck_dependencies")
load("@aspect_bazel_lib//lib:repositories.bzl", "register_jq_toolchains", "register_yq_toolchains")
load("@com_google_cel_cpp//bazel:deps.bzl", "parser_deps")
load("@com_github_chrusty_protoc_gen_jsonschema//:deps.bzl", protoc_gen_jsonschema_go_dependencies = "go_dependencies")

# go version for rules_go
GO_VERSION = "1.20"

JQ_VERSION = "1.7"
YQ_VERSION = "4.24.4"

def envoy_dependency_imports(go_version = GO_VERSION, jq_version = JQ_VERSION, yq_version = YQ_VERSION):
    rules_foreign_cc_dependencies()
    go_rules_dependencies()
    go_register_toolchains(go_version)
    envoy_download_go_sdks(go_version)
    gazelle_dependencies(go_sdk = "go_sdk")
    apple_rules_dependencies()
    pip_dependencies()
    pip_dev_dependencies()
    pip_fuzzing_dependencies()
    rules_pkg_dependencies()
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
    shellcheck_dependencies()
    upb_deps()
    proxy_wasm_rust_sdk_dependencies()
    rules_fuzzing_dependencies(
        oss_fuzz = True,
        honggfuzz = False,
    )
    emscripten_deps(emscripten_version = "3.1.7")
    register_jq_toolchains(version = jq_version)
    register_yq_toolchains(version = yq_version)
    parser_deps()

    # These dependencies, like most of the Go in this repository, exist only for the API.
    # These repos also have transient dependencies - `build_external` allows them to use them.
    # TODO(phlax): remove `build_external` and pin all transients
    go_repository(
        name = "org_golang_google_grpc",
        build_file_proto_mode = "disable",
        importpath = "google.golang.org/grpc",
        sum = "h1:raiipEjMOIC/TO2AvyTxP25XFdLxNIBwzDh3FM3XztI=",
        version = "v1.34.0",
        build_external = "external",
        # project_url = "https://pkg.go.dev/google.golang.org/grpc",
        # last_update = "2020-12-02"
        # use_category = ["api"],
        # cpe = "cpe:2.3:a:grpc:grpc:*",
    )
    go_repository(
        name = "org_golang_x_net",
        importpath = "golang.org/x/net",
        sum = "h1:0mm1VjtFUOIlE1SbDlwjYaDxZVDP2S5ou6y0gSgXHu8=",
        version = "v0.0.0-20200226121028-0de0cce0169b",
        build_external = "external",
        # project_url = "https://pkg.go.dev/golang.org/x/net",
        # last_update = "2020-02-26"
        # use_category = ["api"],
        # source = "https://github.com/bufbuild/protoc-gen-validate/blob/v0.6.1/dependencies.bzl#L129-L134"
    )
    go_repository(
        name = "org_golang_x_text",
        importpath = "golang.org/x/text",
        sum = "h1:cokOdA+Jmi5PJGXLlLllQSgYigAEfHXJAERHVMaCc2k=",
        version = "v0.3.3",
        build_external = "external",
        # project_url = "https://pkg.go.dev/golang.org/x/text",
        # last_update = "2021-06-16"
        # use_category = ["api"],
        # source = "https://github.com/bufbuild/protoc-gen-validate/blob/v0.6.1/dependencies.bzl#L148-L153"
    )
    go_repository(
        name = "org_golang_google_protobuf",
        importpath = "google.golang.org/protobuf",
        sum = "h1:d0NfwRgPtno5B1Wa6L2DAG+KivqkdutMf1UhdNx175w=",
        version = "v1.28.1",
        build_external = "external",
    )
    go_repository(
        name = "com_github_cncf_xds_go",
        importpath = "github.com/cncf/xds/go",
        sum = "h1:B/lvg4tQ5hfFZd4V2hcSfFVfUvAK6GSFKxIIzwnkv8g=",
        version = "v0.0.0-20220520190051-1e77728a1eaa",
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
        name = "com_github_lyft_protoc_gen_star",
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

    protoc_gen_jsonschema_go_dependencies()

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

load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")
load("@io_bazel_rules_go//go:deps.bzl", "go_register_toolchains", "go_rules_dependencies")
load("@bazel_gazelle//:deps.bzl", "gazelle_dependencies", "go_repository")
load("@build_bazel_rules_apple//apple:repositories.bzl", "apple_rules_dependencies")
load("@rules_fuzzing//fuzzing:repositories.bzl", "rules_fuzzing_dependencies")
load("@upb//bazel:workspace_deps.bzl", "upb_deps")
load("@rules_pkg//:deps.bzl", "rules_pkg_dependencies")
load("@rules_rust//rust:repositories.bzl", "rules_rust_dependencies", "rust_register_toolchains")
load("@proxy_wasm_rust_sdk//bazel:dependencies.bzl", "proxy_wasm_rust_sdk_dependencies")
load("@base_pip3//:requirements.bzl", pip_dependencies = "install_deps")
load("@fuzzing_pip3//:requirements.bzl", pip_fuzzing_dependencies = "install_deps")
load("@emsdk//:emscripten_deps.bzl", "emscripten_deps")
load("@com_github_aignas_rules_shellcheck//:deps.bzl", "shellcheck_dependencies")
load("@aspect_bazel_lib//lib:repositories.bzl", "register_jq_toolchains", "register_yq_toolchains")
load("@com_google_cel_cpp//bazel:deps.bzl", "parser_deps")

# go version for rules_go
GO_VERSION = "1.19.2"

JQ_VERSION = "1.6"
YQ_VERSION = "4.25.2"

def envoy_dependency_imports(go_version = GO_VERSION, jq_version = JQ_VERSION, yq_version = YQ_VERSION):
    # TODO: allow building of tools for easier onboarding
    rules_foreign_cc_dependencies(register_default_tools = False, register_built_tools = False)
    go_rules_dependencies()
    go_register_toolchains(go_version)
    gazelle_dependencies()
    apple_rules_dependencies()
    pip_dependencies()
    pip_fuzzing_dependencies()
    rules_pkg_dependencies()
    rules_rust_dependencies()
    rust_register_toolchains(
        include_rustc_srcs = True,
        extra_target_triples = [
            "aarch64-apple-ios",
            "aarch64-apple-ios-sim",
            "aarch64-linux-android",
            "armv7-linux-androideabi",
            "i686-linux-android",
            "wasm32-unknown-unknown",
            "wasm32-wasi",
            "x86_64-apple-ios",
            "x86_64-linux-android",
        ],
    )
    shellcheck_dependencies()
    upb_deps()
    proxy_wasm_rust_sdk_dependencies()
    rules_fuzzing_dependencies(
        oss_fuzz = True,
        honggfuzz = False,
    )
    emscripten_deps()
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
        sum = "h1:WTLtQzmQori5FUH25Pq4WT22oCsv8USpQ+F6rqtsmxw=",
        version = "v1.49.0",
        build_external = "external",
        # project_url = "https://pkg.go.dev/google.golang.org/grpc",
        # last_update = "2022-08-23"
        # use_category = ["api"],
        # cpe = "cpe:2.3:a:grpc:grpc:*",
    )
    go_repository(
        name = "org_golang_x_net",
        importpath = "golang.org/x/net",
        sum = "h1:tvrvnPFcdzp294diPnrdZZZ8XUt2Tyj7svb7X52iDuU=",
        version = "v0.0.0-20221014081412-f15817d10f9b",
        build_external = "external",
        # project_url = "https://pkg.go.dev/golang.org/x/net",
        # last_update = "2022-10-14"
        # use_category = ["api"],
        # source = "https://github.com/bufbuild/protoc-gen-validate/blob/v0.6.7/dependencies.bzl#L129-L134"
    )
    go_repository(
        name = "org_golang_x_text",
        importpath = "golang.org/x/text",
        sum = "h1:BrVqGRd7+k1DiOgtnFvAkoQEWQvBc25ouMJM6429SFg=",
        version = "v0.4.0",
        build_external = "external",
        # project_url = "https://pkg.go.dev/golang.org/x/text",
        # last_update = "2022-10-14"
        # use_category = ["api"],
        # source = "https://github.com/bufbuild/protoc-gen-validate/blob/v0.6.7/dependencies.bzl#L148-L153"
    )
    go_repository(
        name = "com_github_spf13_afero",
        importpath = "github.com/spf13/afero",
        sum = "h1:j49Hj62F0n+DaZ1dDCvhABaPNSGNkt32oRFxI33IEMw=",
        version = "v1.9.2",
        build_external = "external",
        # project_url = "https://pkg.go.dev/github.com/spf13/afero",
        # last_update = "2022-07-19"
        # use_category = ["api"],
        # source = "https://github.com/bufbuild/protoc-gen-validate/blob/v0.6.7/dependencies.bzl#L60-L65"
    )
    go_repository(
        name = "com_github_lyft_protoc_gen_star",
        importpath = "github.com/lyft/protoc-gen-star",
        sum = "h1:erE0rdztuaDq3bpGifD95wfoPrSZc95nGA6tbiNYh6M=",
        version = "v0.6.1",
        build_external = "external",
        # project_url = "https://pkg.go.dev/github.com/lyft/protoc-gen-star",
        # last_update = "2021-11-11"
        # use_category = ["api"],
        # source = "https://github.com/bufbuild/protoc-gen-validate/blob/v0.6.7/dependencies.bzl#L35-L40"
    )
    go_repository(
        name = "com_github_iancoleman_strcase",
        importpath = "github.com/iancoleman/strcase",
        sum = "h1:05I4QRnGpI0m37iZQRuskXh+w77mr6Z41lwQzuHLwW0=",
        version = "v0.2.0",
        build_external = "external",
        # project_url = "https://pkg.go.dev/github.com/iancoleman/strcase",
        # last_update = "2021-06-12"
        # use_category = ["api"],
        # source = "https://github.com/bufbuild/protoc-gen-validate/blob/v0.6.7/dependencies.bzl#L23-L28"
    )

load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")
load("@io_bazel_rules_go//go:deps.bzl", "go_register_toolchains", "go_rules_dependencies")
load("@envoy_build_tools//toolchains:rbe_toolchains_config.bzl", "rbe_toolchains_config")
load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_rbe_exec_properties_dict", "custom_exec_properties")
load("@bazel_gazelle//:deps.bzl", "gazelle_dependencies", "go_repository")
load("@build_bazel_rules_apple//apple:repositories.bzl", "apple_rules_dependencies")
load("@rules_fuzzing//fuzzing:repositories.bzl", "oss_fuzz_dependencies", "rules_fuzzing_dependencies")
load("@upb//bazel:workspace_deps.bzl", "upb_deps")
load("@io_bazel_rules_rust//rust:repositories.bzl", "rust_repositories")
load("@config_validation_pip3//:requirements.bzl", config_validation_pip_install = "pip_install")
load("@configs_pip3//:requirements.bzl", configs_pip_install = "pip_install")
load("@headersplit_pip3//:requirements.bzl", headersplit_pip_install = "pip_install")
load("@kafka_pip3//:requirements.bzl", kafka_pip_install = "pip_install")
load("@protodoc_pip3//:requirements.bzl", protodoc_pip_install = "pip_install")
load("@thrift_pip3//:requirements.bzl", thrift_pip_install = "pip_install")
load("@fuzzing_pip3//:requirements.bzl", fuzzing_pip_install = "pip_install")
load("@rules_antlr//antlr:deps.bzl", "antlr_dependencies")
load("@proxy_wasm_rust_sdk//bazel:dependencies.bzl", "proxy_wasm_rust_sdk_dependencies")

# go version for rules_go
GO_VERSION = "1.15.5"

def envoy_dependency_imports(go_version = GO_VERSION):
    rules_foreign_cc_dependencies()
    go_rules_dependencies()
    go_register_toolchains(go_version)
    rbe_toolchains_config()
    gazelle_dependencies()
    apple_rules_dependencies()
    rust_repositories()
    upb_deps()
    antlr_dependencies(472)
    proxy_wasm_rust_sdk_dependencies()
    rules_fuzzing_dependencies()
    oss_fuzz_dependencies()

    custom_exec_properties(
        name = "envoy_large_machine_exec_property",
        constants = {
            "LARGE_MACHINE": create_rbe_exec_properties_dict(labels = dict(size = "large")),
        },
    )

    # These dependencies, like most of the Go in this repository, exist only for the API.
    go_repository(
        name = "org_golang_google_grpc",
        build_file_proto_mode = "disable",
        importpath = "google.golang.org/grpc",
        sum = "h1:EC2SB8S04d2r73uptxphDSUG+kTKVgjRPF+N3xpxRB4=",
        version = "v1.29.1",
    )
    go_repository(
        name = "org_golang_x_net",
        importpath = "golang.org/x/net",
        sum = "h1:fHDIZ2oxGnUZRN6WgWFCbYBjH9uqVPRCUVUDhs0wnbA=",
        version = "v0.0.0-20190813141303-74dc4d7220e7",
    )
    go_repository(
        name = "org_golang_x_text",
        importpath = "golang.org/x/text",
        sum = "h1:g61tztE5qeGQ89tm6NTjjM9VPIm088od1l6aSorWRWg=",
        version = "v0.3.0",
    )
    go_repository(
        name = "com_github_spf13_afero",
        importpath = "github.com/spf13/afero",
        sum = "h1:8q6vk3hthlpb2SouZcnBVKboxWQWMDNF38bwholZrJc=",
        version = "v1.3.4",
    )
    go_repository(
        name = "com_github_lyft_protoc_gen_star",
        importpath = "github.com/lyft/protoc-gen-star",
        sum = "h1:sImehRT+p7lW9n6R7MQc5hVgzWGEkDVZU4AsBQ4Isu8=",
        version = "v0.5.1",
    )
    go_repository(
        name = "com_github_iancoleman_strcase",
        importpath = "github.com/iancoleman/strcase",
        sum = "h1:ux/56T2xqZO/3cP1I2F86qpeoYPCOzk+KF/UH/Ar+lk=",
        version = "v0.0.0-20180726023541-3605ed457bf7",
    )

    config_validation_pip_install()
    configs_pip_install()
    headersplit_pip_install()
    kafka_pip_install()
    protodoc_pip_install()
    thrift_pip_install()
    fuzzing_pip_install()

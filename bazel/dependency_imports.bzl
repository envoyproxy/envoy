load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")
load("@io_bazel_rules_go//go:deps.bzl", "go_register_toolchains", "go_rules_dependencies")
load("@envoy_build_tools//toolchains:rbe_toolchains_config.bzl", "rbe_toolchains_config")
load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_rbe_exec_properties_dict", "custom_exec_properties")
load("@bazel_gazelle//:deps.bzl", "gazelle_dependencies", "go_repository")
load("@build_bazel_rules_apple//apple:repositories.bzl", "apple_rules_dependencies")
load("@upb//bazel:repository_defs.bzl", upb_bazel_version_repository = "bazel_version_repository")
load("@config_validation_pip3//:requirements.bzl", config_validation_pip_install = "pip_install")
load("@protodoc_pip3//:requirements.bzl", protodoc_pip_install = "pip_install")

# go version for rules_go
GO_VERSION = "1.14.4"

def envoy_dependency_imports(go_version = GO_VERSION):
    rules_foreign_cc_dependencies()
    go_rules_dependencies()
    go_register_toolchains(go_version)
    rbe_toolchains_config()
    gazelle_dependencies()
    apple_rules_dependencies()
    upb_bazel_version_repository(name = "upb_bazel_version")

    custom_exec_properties(
        name = "envoy_large_machine_exec_property",
        constants = {
            "LARGE_MACHINE": create_rbe_exec_properties_dict(labels = dict(size = "large")),
        },
    )

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

    config_validation_pip_install()
    protodoc_pip_install()

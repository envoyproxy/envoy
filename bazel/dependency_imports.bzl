load("@bazel_gazelle//:deps.bzl", "gazelle_dependencies", "go_repository")
load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_rbe_exec_properties_dict", "custom_exec_properties")
load("@build_bazel_rules_apple//apple:repositories.bzl", "apple_rules_dependencies")
load("@envoy_build_tools//toolchains:rbe_toolchains_config.bzl", "rbe_toolchains_config")
load("@io_bazel_rules_go//go:deps.bzl", "go_register_toolchains", "go_rules_dependencies")
load("@rules_antlr//antlr:deps.bzl", "antlr_dependencies")
load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")
load("@upb//bazel:workspace_deps.bzl", "upb_deps")
load("@config_validation_pip3//:requirements.bzl", config_validation_pip_install = "pip_install")
load("@configs_pip3//:requirements.bzl", configs_pip_install = "pip_install")
load("@headersplit_pip3//:requirements.bzl", headersplit_pip_install = "pip_install")
load("@kafka_pip3//:requirements.bzl", kafka_pip_install = "pip_install")
load("@protodoc_pip3//:requirements.bzl", protodoc_pip_install = "pip_install")
load("@thrift_pip3//:requirements.bzl", thrift_pip_install = "pip_install")

# go version for rules_go
GO_VERSION = "1.14.9"

def envoy_dependency_imports(go_version = GO_VERSION):
    apple_rules_dependencies()
    go_register_toolchains(go_version)
    gazelle_dependencies()
    go_rules_dependencies()
    rbe_toolchains_config()
    rules_foreign_cc_dependencies()
    upb_deps()
    antlr_dependencies(471)

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
        sum = "h1:zWTV+LMdc3kaiJMSTOFz2UgSBgx8RNQoTGiZu3fR9S0=",
        version = "v1.32.0",
        # project_url = "https://pkg.go.dev/google.golang.org/grpc",
        # last_update = "2020-09-08"
        # use_category = ["api"],
        # cpe = "cpe:2.3:a:grpc:grpc:*",
    )
    go_repository(
        name = "org_golang_x_net",
        importpath = "golang.org/x/net",
        sum = "h1:dk0ukUIHmGHqASjP0iue2261isepFCC6XRCSd1nHgDw=",
        version = "v0.0.0-20201002202402-0a1ea396d57c",
        # project_url = "https://pkg.go.dev/mod/golang.org/x/net",
        # last_update = "2020-10-02"
        # use_category = ["api"],
    )
    go_repository(
        name = "org_golang_x_text",
        importpath = "golang.org/x/text",
        sum = "h1:cokOdA+Jmi5PJGXLlLllQSgYigAEfHXJAERHVMaCc2k=",
        version = "v0.3.3",
        # project_url = "https://pkg.go.dev/mod/golang.org/x/text",
        # last_update = "2020-06-16"
        # use_category = ["api"],
    )

    config_validation_pip_install()
    configs_pip_install()
    headersplit_pip_install()
    kafka_pip_install()
    protodoc_pip_install()
    thrift_pip_install()

load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")
load("@io_bazel_rules_go//go:deps.bzl", "go_register_toolchains", "go_rules_dependencies")
load("@envoy//bazel/toolchains:rbe_toolchains_config.bzl", "rbe_toolchains_config")
load("@bazel_gazelle//:deps.bzl", "gazelle_dependencies", "go_repository")

# go version for rules_go
GO_VERSION = "1.12.8"

def envoy_dependency_imports(go_version = GO_VERSION):
    go_repository(
        name = "com_github_gogo_protobuf",
        importpath = "github.com/gogo/protobuf",
        sum = "h1:G8O7TerXerS4F6sx9OV7/nRfJdnXgHZu/S/7F2SN+UE=",
        version = "v1.3.0",
    )

    rules_foreign_cc_dependencies()
    go_rules_dependencies()
    go_register_toolchains(go_version)
    rbe_toolchains_config()
    gazelle_dependencies()

    go_repository(
        name = "org_golang_google_grpc",
        build_file_proto_mode = "disable",
        importpath = "google.golang.org/grpc",
        sum = "h1:AzbTB6ux+okLTzP8Ru1Xs41C303zdcfEht7MQnYJt5A=",
        version = "v1.23.0",
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
        name = "io_istio_gogo_genproto",
        importpath = "istio.io/gogo-genproto",
        commit = "ee07f27854802700b55adf4cf76bbcefed623563",
    )

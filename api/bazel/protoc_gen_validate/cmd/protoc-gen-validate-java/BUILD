load("@bazel_gazelle//:def.bzl", "gazelle")
load("@io_bazel_rules_go//go:def.bzl", "go_binary", "go_library")

# gazelle:prefix github.com/envoyproxy/protoc-gen-validate
# gazelle:exclude tests
# gazelle:exclude example-workspace
# gazelle:exclude validate/validate.h
# gazelle:go_naming_convention import_alias
gazelle(name = "gazelle")

go_binary(
    name = "protoc-gen-validate-java",
    embed = [":protoc-gen-validate_lib"],
    importpath = "github.com/envoyproxy/protoc-gen-validate/cmd/protoc-gen-validate-java",
    visibility = ["//visibility:public"],
)

go_library(
    name = "protoc-gen-validate-java_lib",
    srcs = ["main.go"],
    importpath = "github.com/envoyproxy/protoc-gen-validate/cmd/protoc-gen-validate-java",
    visibility = ["//visibility:private"],
    deps = [
        "//module",
        "@com_github_lyft_protoc_gen_star_v2//:protoc-gen-star",
        "@com_github_lyft_protoc_gen_star_v2//lang/go",
        "@org_golang_google_protobuf//types/pluginpb",
    ],
)

go_library(
    name = "protoc-gen-validate_lib",
    srcs = ["main.go"],
    importpath = "github.com/envoyproxy/protoc-gen-validate",
    visibility = ["//visibility:private"],
    deps = [
        "//module",
        "@com_github_lyft_protoc_gen_star_v2//:protoc-gen-star",
        "@org_golang_google_protobuf//types/pluginpb",
    ],
)

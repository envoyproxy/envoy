load("@io_bazel_rules_go//go:def.bzl", "go_binary", "go_library")


go_binary(
    name = "protoc-gen-jsonschema",
    embed = [":go_default_library"],
    importpath = "github.com/chrusty/protoc-gen-jsonschema",
    visibility = ["//visibility:public"],
)


go_library(
    name = "go_default_library",
    srcs = ["cmd/protoc-gen-jsonschema/main.go"],
    importpath = "github.com/chrusty/protoc-gen-jsonschema",
    visibility = ["//visibility:private"],
    deps = [
        "@com_github_golang_protobuf//proto:go_default_library",
        "@com_github_golang_protobuf//protoc-gen-go/plugin:go_default_library",
        "@com_github_sirupsen_logrus//:go_default_library",
        ":go_internal_library"
    ],
)


go_library(
    name = "go_internal_library",
    srcs = [
        "internal/converter/proto_package.go",
        "internal/converter/sourcecodeinfo.go",
        "internal/converter/converter.go",
        "internal/converter/types.go",
    ],
    importpath = "github.com/chrusty/protoc-gen-jsonschema/internal/converter",
    visibility = ["//visibility:public"],
    deps = [
        "@com_github_golang_protobuf//proto:go_default_library",
        "@com_github_golang_protobuf//protoc-gen-go/plugin:go_default_library",
        "@com_github_golang_protobuf//protoc-gen-go/descriptor:go_default_library",
        "@com_github_sirupsen_logrus//:go_default_library",
        "@com_github_xeipuuv_gojsonpointer//:go_default_library",
        "@com_github_xeipuuv_gojsonreference//:go_default_library",
        "@com_github_xeipuuv_gojsonschema//:go_default_library",
        "@com_github_iancoleman_orderedmap//:go_default_library",
        "@com_github_alecthomas_jsonschema//:go_default_library",
    ],
)

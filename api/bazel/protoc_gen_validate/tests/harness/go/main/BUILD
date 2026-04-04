load("@io_bazel_rules_go//go:def.bzl", "go_binary", "go_library")

go_library(
    name = "go_default_library",
    srcs = ["harness.go"],
    importpath = "github.com/envoyproxy/protoc-gen-validate/tests/harness/go/main",
    visibility = ["//visibility:private"],
    deps = [
        "//tests/harness:harness_go_proto",
        "//tests/harness/cases:go",
        "//tests/harness/cases/other_package:go",
        "//tests/harness/cases/sort:go",
        "//tests/harness/cases/yet_another_package:go",
        "@org_golang_google_protobuf//proto:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb:go_default_library",
        "@org_golang_google_protobuf//types/known/durationpb:go_default_library",
        "@org_golang_google_protobuf//types/known/timestamppb:go_default_library",
    ],
)

genrule(
    name = "go-harness-bin",
    srcs = [":main"],
    outs = ["go-harness"],
    cmd = "cp $(SRCS) $@",
    visibility = ["//visibility:public"],
)

genrule(
    name = "go-harness-exe",
    srcs = [":main"],
    outs = ["go-harness.exe"],
    cmd = "cp $(SRCS) $@",
    visibility = ["//visibility:public"],
)

go_binary(
    name = "main",
    embed = [":go_default_library"],
    importpath = "github.com/envoyproxy/protoc-gen-validate/tests/harness/go/main",
    visibility = ["//visibility:public"],
)

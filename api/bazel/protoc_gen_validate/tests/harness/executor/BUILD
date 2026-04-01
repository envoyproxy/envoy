load("@io_bazel_rules_go//go:def.bzl", "go_binary", "go_library")

config_setting(
    name = "windows_x86_64",
    values = {"cpu": "x64_windows"},
)

go_library(
    name = "go_default_library",
    srcs = [
        "cases.go",
        "executor.go",
        "harness.go",
        "worker.go",
    ],
    importpath = "github.com/envoyproxy/protoc-gen-validate/tests/harness/executor",
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
        "@org_golang_google_protobuf//types/known/wrapperspb:go_default_library",
        "@org_golang_x_net//context:go_default_library",
    ],
)

go_binary(
    name = "executor",
    data = ["//tests/harness/cc:cc-harness"] + select({
        ":windows_x86_64": [
            "//tests/harness/go/main:go-harness-exe",
        ],
        "//conditions:default": [
            "//tests/harness/go/main:go-harness-bin",
            "//tests/harness/java:java-harness",
            "//tests/harness/python:python-harness",
        ],
    }),
    embed = [":go_default_library"],
    importpath = "github.com/envoyproxy/protoc-gen-validate/tests/harness/executor",
    visibility = ["//visibility:private"],
)

[
    sh_test(
        name = "executor_" + lang + "_test",
        srcs = ["executor_test.sh"],
        args = [
            "$(location :executor)",
            "-" + lang,
        ],
        data = [":executor"],
        # This could be sharded more, but each shard incurs overhead and test
        # execution is already sharded by having separate test rules for each language.
        shard_count = 5,
        deps = ["@bazel_tools//tools/bash/runfiles"],
    )
    for lang in ("cc", "go", "python")
]

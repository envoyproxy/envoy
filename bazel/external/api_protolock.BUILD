load("@io_bazel_rules_go//go:def.bzl", "go_binary", "go_library")

licenses(["notice"])  # Apache 2

go_binary(
    name = "protolock",
    srcs = [
        "cmd/protolock/main.go",
        "cmd/protolock/plugins.go",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":protolock_extend",
        ":protolock_root",
    ],
)

go_library(
    name = "protolock_extend",
    srcs = [
        "extend/plugin.go",
    ],
    importpath = "github.com/nilslice/protolock/extend",
    deps = [
        ":protolock_root",
    ],
)

go_library(
    name = "protolock_root",
    srcs = [
        "commit.go",
        "config.go",
        "hints.go",
        "init.go",
        "parse.go",
        "protopath.go",
        "report.go",
        "rules.go",
        "status.go",
        "uptodate.go",
    ],
    importpath = "github.com/nilslice/protolock",
    deps = [
        "@com_github_emicklei_proto//:proto_parsing_lib",
    ],
)

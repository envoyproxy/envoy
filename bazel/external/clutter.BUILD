load("@io_bazel_rules_go//go:def.bzl", "go_binary", "go_library")

go_binary(
    name = "clutter",
    embed = [":go_default_library"],
    gotags = ["nofsnotify"],
    importpath = "github.com/cluttercode/clutter",
    visibility = ["//visibility:public"],
)

go_library(
    name = "go_default_library",
    srcs = glob(["cmd/clutter/*.go"]),
    importpath = "github.com/cluttercode/clutter",
    visibility = ["//visibility:public"],
    deps = [
        ":go_index_library",
        ":go_linter_library",
        ":go_parser_library",
        ":go_resolver_library",
        ":go_scanner_library",
        ":go_strmatcher_library",
        ":go_zlog_library",
        "@com_github_urfave_cli//:go_default_library",
        "@in_gopkg_yaml_v2//:go_default_library",
    ],
)

go_library(
    name = "go_resolver_library",
    srcs = glob(["internal/pkg/resolver/*.go"]),
    importpath = "github.com/cluttercode/clutter/internal/pkg/resolver",
    visibility = ["//visibility:public"],
    deps = [
        ":go_index_library",
        ":go_zlog_library",
    ],
)

go_library(
    name = "go_scanner_library",
    srcs = glob(["internal/pkg/scanner/*.go"]),
    importpath = "github.com/cluttercode/clutter/internal/pkg/scanner",
    visibility = ["//visibility:public"],
    deps = [
        ":go_gitignore_library",
        ":go_zlog_library",
    ],
)

go_library(
    name = "go_parser_library",
    srcs = glob(["internal/pkg/parser/*.go"]),
    importpath = "github.com/cluttercode/clutter/internal/pkg/parser",
    visibility = ["//visibility:public"],
    deps = [
        ":go_index_library",
        ":go_scanner_library",
    ],
)

go_library(
    name = "go_index_library",
    srcs = glob(["internal/pkg/index/*.go"]),
    importpath = "github.com/cluttercode/clutter/internal/pkg/index",
    visibility = ["//visibility:public"],
    deps = [
        ":go_scanner_library",
        ":go_strmatcher_library",
    ],
)

go_library(
    name = "go_linter_library",
    srcs = glob(["internal/pkg/linter/*.go"]),
    importpath = "github.com/cluttercode/clutter/internal/pkg/linter",
    visibility = ["//visibility:public"],
    deps = [
        ":go_index_library",
        ":go_strmatcher_library",
        ":go_zlog_library",
    ],
)

go_library(
    name = "go_zlog_library",
    srcs = glob(["pkg/zlog/*.go"]),
    importpath = "github.com/cluttercode/clutter/pkg/zlog",
    visibility = ["//visibility:public"],
)

go_library(
    name = "go_strmatcher_library",
    srcs = glob(["pkg/strmatcher/*.go"]),
    importpath = "github.com/cluttercode/clutter/pkg/strmatcher",
    visibility = ["//visibility:public"],
    deps = [":go_gitignore_library"],
)

go_library(
    name = "go_gitignore_library",
    srcs = glob(["pkg/gitignore/*.go"]),
    importpath = "github.com/cluttercode/clutter/pkg/gitignore",
    visibility = ["//visibility:public"],
)

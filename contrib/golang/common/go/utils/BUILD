load("@rules_go//go:def.bzl", "go_library", "go_test")

licenses(["notice"])  # Apache 2

go_library(
    name = "utils",
    srcs = ["string.go"],
    importpath = "github.com/envoyproxy/envoy/contrib/golang/common/go/utils",
    visibility = ["//visibility:public"],
)

go_test(
    name = "utils_test",
    srcs = ["string_test.go"],
    embed = [":utils"],
)

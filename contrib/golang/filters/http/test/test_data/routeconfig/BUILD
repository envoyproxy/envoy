load("@io_bazel_rules_go//go:def.bzl", "go_library")

licenses(["notice"])  # Apache 2

go_library(
    name = "routeconfig",
    srcs = [
        "config.go",
        "filter.go",
    ],
    cgo = True,
    importpath = "example.com/test-data/routeconfig",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/common/go/api",
        "//contrib/golang/filters/http/source/go/pkg/http",
        "@com_github_cncf_xds_go//xds/type/v3:type",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/structpb",
    ],
)

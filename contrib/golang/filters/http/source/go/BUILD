load("@io_bazel_rules_go//go:def.bzl", "go_binary")

licenses(["notice"])  # Apache 2

go_binary(
    name = "libgolang.so",
    srcs = [
        "api.h",
        "export.go",
        "main.go",
    ],
    out = "libgolang.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/filters/http/source/go/pkg/api",
        "//contrib/golang/filters/http/source/go/pkg/http",
        "//contrib/golang/filters/http/source/go/pkg/utils",
        "@com_github_cncf_xds_go//udpa/type/v1:type",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

load("@io_bazel_rules_go//go:def.bzl", "go_binary")

licenses(["notice"])  # Apache 2

go_binary(
    name = "filter.so",
    srcs = [
        "filter.go",
    ],
    out = "filter.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/filters/http/test/test_data/passthrough",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/common/go/api",
        "//contrib/golang/filters/http/source/go/pkg/http",
        "@com_github_cncf_xds_go//udpa/type/v1:type",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

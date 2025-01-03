load("@io_bazel_rules_go//go:def.bzl", "go_binary")

licenses(["notice"])  # Apache 2

go_binary(
    name = "filter.so",
    srcs = [
        "config.go",
        # "dubbo.apache.org/dubbo-go/v3/common/constant",
        # "dubbo.apache.org/dubbo-go/v3/filter/generic/generalizer",
        # "dubbo.apache.org/dubbo-go/v3/protocol",
        # "dubbo.apache.org/dubbo-go/v3/protocol/dubbo",
        # "dubbo.apache.org/dubbo-go/v3/protocol/invocation",
        # "dubbo.apache.org/dubbo-go/v3/remoting",
        "filter.go",
        # "github.com/apache/dubbo-go-hessian2",
        # "github.com/cncf/xds/go/xds/type/v3",
    ],
    out = "filter.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/upstreams/http/tcp/test/test_data",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/common/go/api",
        "//contrib/golang/upstreams/http/tcp/source/go/pkg/upstreams/http/tcp:http_tcp_bridge",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/structpb",
    ],
)

load("@io_bazel_rules_go//go:def.bzl", "go_binary")

licenses(["notice"])  # Apache 2

go_binary(
    name = "bridges.so",
    srcs = [
        "bridges.go",
    ],
    out = "bridges.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/upstreams/http/tcp/test/test_data",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/common/go/api",
        "//contrib/golang/upstreams/http/tcp/source/go/pkg/upstreams/http/tcp:http_tcp_bridge",
        "//contrib/golang/upstreams/http/tcp/test/test_data/add_data",
        "//contrib/golang/upstreams/http/tcp/test/test_data/buffered",
        "//contrib/golang/upstreams/http/tcp/test/test_data/header_op",
        "//contrib/golang/upstreams/http/tcp/test/test_data/local_reply",
        "//contrib/golang/upstreams/http/tcp/test/test_data/panic",
        "//contrib/golang/upstreams/http/tcp/test/test_data/property",
        "//contrib/golang/upstreams/http/tcp/test/test_data/self_half_close",
        "//contrib/golang/upstreams/http/tcp/test/test_data/streaming",
    ],
)

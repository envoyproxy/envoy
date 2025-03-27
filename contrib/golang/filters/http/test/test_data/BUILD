load("@io_bazel_rules_go//go:def.bzl", "go_binary")

licenses(["notice"])  # Apache 2

go_binary(
    name = "plugins.so",
    srcs = [
        "plugins.go",
    ],
    out = "plugins.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/filters/http/test/test_data",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/common/go/api",
        "//contrib/golang/filters/http/source/go/pkg/http",
        "//contrib/golang/filters/http/test/test_data/access_log",
        "//contrib/golang/filters/http/test/test_data/action",
        "//contrib/golang/filters/http/test/test_data/add_data",
        "//contrib/golang/filters/http/test/test_data/basic",
        "//contrib/golang/filters/http/test/test_data/buffer",
        "//contrib/golang/filters/http/test/test_data/bufferinjectdata",
        "//contrib/golang/filters/http/test/test_data/destroyconfig",
        "//contrib/golang/filters/http/test/test_data/echo",
        "//contrib/golang/filters/http/test/test_data/metric",
        "//contrib/golang/filters/http/test/test_data/passthrough",
        "//contrib/golang/filters/http/test/test_data/property",
        "//contrib/golang/filters/http/test/test_data/routeconfig",
        "//contrib/golang/filters/http/test/test_data/secrets",
        "//contrib/golang/filters/http/test/test_data/websocket",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/structpb",
    ],
)

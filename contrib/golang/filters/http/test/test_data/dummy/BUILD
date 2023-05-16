load("@io_bazel_rules_go//go:def.bzl", "go_binary")

licenses(["notice"])  # Apache 2

go_binary(
    name = "my_plugin.so",
    srcs = ["plugin.go"],
    out = "my_plugin.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/filters/http/test/test_data/dummy",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/filters/http/source/go/pkg/http",
    ],
)

go_binary(
    name = "my_other_plugin.so",
    srcs = ["plugin.go"],
    out = "my_other_plugin.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/filters/http/test/test_data/dummy",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/filters/http/source/go/pkg/http",
    ],
)

go_binary(
    name = "my_configurable_plugin.so",
    srcs = ["plugin.go"],
    out = "my_configurable_plugin.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/filters/http/test/test_data/dummy",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/filters/http/source/go/pkg/http",
    ],
)

go_binary(
    name = "simple.so",
    srcs = ["plugin.go"],
    out = "simple.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/filters/http/test/test_data/dummy",
    linkmode = "c-shared",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/filters/http/source/go/pkg/http",
    ],
)

filegroup(
    name = "testing_shared_objects",
    srcs = [
        ":my_configurable_plugin.so",
        ":my_other_plugin.so",
        ":my_plugin.so",
        ":simple.so",
    ],
    visibility = ["//visibility:public"],
)

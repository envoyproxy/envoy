load("@io_bazel_rules_go//go:def.bzl", "go_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

go_binary(
    name = "my_plugin.so",
    srcs = [
        "config.go",
        "plugin.go",
    ],
    out = "my_plugin.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/filters/http/test/test_data/echo",
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

go_binary(
    name = "my_other_plugin.so",
    srcs = [
        "other_config.go",
        "plugin.go",
    ],
    out = "my_other_plugin.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/filters/http/test/test_data/echo",
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

go_binary(
    name = "my_configurable_plugin.so",
    srcs = [
        "configurable_config.go",
        "plugin.go",
    ],
    out = "my_configurable_plugin.so",
    cgo = True,
    importpath = "github.com/envoyproxy/envoy/contrib/golang/filters/http/test/test_data/echo",
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

filegroup(
    name = "testing_shared_objects",
    srcs = [
        ":my_plugin.so",
        ":my_other_plugin.so",
        ":my_configurable_plugin.so",
    ]
)

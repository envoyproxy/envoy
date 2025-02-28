load("@io_bazel_rules_go//go:def.bzl", "go_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test_library(
    name = "destroyconfig_test_lib",
    hdrs = [
        "api.h",
        "destroyconfig.h",
    ],
)

go_library(
    name = "destroyconfig",
    srcs = [
        "api.h",
        "config.go",
        "destroyconfig.h",
    ],
    cgo = True,
    importpath = "example.com/test-data/destroyconfig",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/common/go/api",
        "//contrib/golang/filters/http/source/go/pkg/http",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

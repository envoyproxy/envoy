load("@io_bazel_rules_go//go:def.bzl", "go_library")

licenses(["notice"])  # Apache 2

go_library(
    name = "http",
    srcs = [
        "api.h",
        "capi_impl.go",
        "config.go",
        "filter.go",
        "filtermanager.go",
        "passthrough.go",
        "shim.go",
        "type.go",
    ],
    cgo = True,
    clinkopts = select({
        "@io_bazel_rules_go//go/platform:android": [
            "-Wl,-unresolved-symbols=ignore-all",
        ],
        "@io_bazel_rules_go//go/platform:darwin": [
            "-Wl,-undefined,dynamic_lookup",
        ],
        "@io_bazel_rules_go//go/platform:ios": [
            "-Wl,-undefined,dynamic_lookup",
        ],
        "@io_bazel_rules_go//go/platform:linux": [
            "-Wl,-unresolved-symbols=ignore-all",
        ],
        "//conditions:default": [],
    }),
    importpath = "github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/http",
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/common/go/api",
        "//contrib/golang/common/go/api_impl",
        "//contrib/golang/common/go/utils",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/structpb",
    ],
)

load("@io_bazel_rules_go//go:def.bzl", "go_library")

go_library(
    name = "module",
    srcs = [
        "checker.go",
        "validate.go",
    ],
    importpath = "github.com/envoyproxy/protoc-gen-validate/module",
    visibility = ["//visibility:public"],
    deps = [
        "//templates",
        "//templates/java",
        "//validate:validate_go",
        "@com_github_lyft_protoc_gen_star_v2//:protoc-gen-star",
        "@com_github_lyft_protoc_gen_star_v2//lang/go",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/durationpb",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)

alias(
    name = "go_default_library",
    actual = ":module",
    deprecation = "Use :module instead of :go_default_library.  Details about the new naming convention: https://github.com/bazelbuild/bazel-gazelle/pull/863",
    visibility = ["//visibility:public"],
)

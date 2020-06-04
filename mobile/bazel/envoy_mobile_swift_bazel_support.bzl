load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def swift_support():
    rules_foreign_cc_dependencies()

    http_archive(
        name = "build_bazel_apple_support",
        sha256 = "595a6652d8d65380a3d764826bf1a856a8cc52371bbd961dfcd942fdb14bc133",
        strip_prefix = "apple_support-e16463ef91ed77622c17441f9569bda139d45b18",
        urls = ["https://github.com/bazelbuild/apple_support/archive/e16463ef91ed77622c17441f9569bda139d45b18.tar.gz"],
    )

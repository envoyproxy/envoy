load("@io_bazel_rules_go//go:def.bzl", "go_test")

def api_go_test(name, **kwargs):
    go_test(
        name = name,
        **kwargs
    )

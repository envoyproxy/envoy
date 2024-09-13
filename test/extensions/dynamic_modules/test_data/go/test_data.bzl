load("@io_bazel_rules_go//go:def.bzl", "go_binary")

# This declares a go_binary target that is used to build a shared Go library.
# name + ".go" is the source file that is compiled to create the shared library.
def test_program(name):
    go_binary(
        name = name,
        srcs = [name + ".go"],
        out = "lib" + name + ".so",
        cgo = True,
        importpath = "github.com/envoyproxy/envoy/test/extensions/dynmic_modules/sdk/go",
        linkmode = "c-shared",
        deps = [
            "//source/extensions/dynamic_modules/sdk/go:envoy_dynamic_modules_go_sdk",
        ],
    )

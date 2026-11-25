load("@rules_go//go:def.bzl", "go_binary")

# Builds a Go shared library for Envoy's dynamic module tests.
def test_program(name, extra_srcs = [], gotags = []):
    go_binary(
        name = name,
        srcs = ["{}/{}.go".format(name, name)] + extra_srcs,
        out = "lib{}.so".format(name),
        cgo = True,
        gotags = gotags,
        linkmode = "c-shared",
        visibility = ["//visibility:public"],
        deps = [
            "@envoy//source/extensions/dynamic_modules:go_sdk",
            "@envoy//source/extensions/dynamic_modules:go_sdk_shared",
            "@envoy//source/extensions/dynamic_modules:go_sdk_abi",
        ],
    )

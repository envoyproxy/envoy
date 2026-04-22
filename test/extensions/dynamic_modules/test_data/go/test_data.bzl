load("@io_bazel_rules_go//go:def.bzl", "go_binary")

# This declares a cc_library target that is used to build a shared library.
# name + ".c" is the source file that is compiled to create the shared library.
def test_program(name):
    go_binary(
        name = name,
        srcs = ["{}/{}.go".format(name, name)],
        out = "lib{}.so".format(name),
        cgo = True,
        linkmode = "c-shared",
        visibility = ["//visibility:public"],
        deps = [
            "@envoy//source/extensions/dynamic_modules:go_sdk",
            "@envoy//source/extensions/dynamic_modules:go_sdk_shared",
            "@envoy//source/extensions/dynamic_modules:go_sdk_abi",
        ],
    )

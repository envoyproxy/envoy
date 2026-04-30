load("@io_bazel_rules_go//go:def.bzl", "go_binary")

# Builds a Go-based test_data dynamic module as a c-shared .so.
#
# The Go SDK's `abi/*.go` files reference all of Envoy's per-surface
# `envoy_dynamic_module_callback_*` symbols. When a test binary loads the .so but only
# links a subset of the matching `abi_impl` C++ libraries, the unresolved references
# would break dlopen on Linux. The `-Wl,--unresolved-symbols=ignore-all` linker flag
# tells the dynamic linker to allow these unresolved symbols at load time; they're
# resolved lazily when the module actually invokes them, and any genuinely missing
# symbol surfaces as a clear error at first call rather than at dlopen.
def test_program(name):
    go_binary(
        name = name,
        srcs = ["{}/{}.go".format(name, name)],
        out = "lib{}.so".format(name),
        cgo = True,
        linkmode = "c-shared",
        gc_linkopts = select({
            "@platforms//os:linux": [
                "-extldflags=-Wl,--unresolved-symbols=ignore-all",
            ],
            "@platforms//os:macos": [
                "-extldflags=-Wl,-undefined,dynamic_lookup",
            ],
            "//conditions:default": [],
        }),
        visibility = ["//visibility:public"],
        deps = [
            "@envoy//source/extensions/dynamic_modules:go_sdk",
            "@envoy//source/extensions/dynamic_modules:go_sdk_shared",
            "@envoy//source/extensions/dynamic_modules:go_sdk_abi",
        ],
    )

load("@envoy_rust_crate_index//:defs.bzl", "crate_deps")
load("@rules_rust//rust:defs.bzl", "rust_binary")
load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//bazel:toolchains.bzl", "LLVM_MAJOR")

licenses(["notice"])  # Apache 2

envoy_package()

# Generates the Rust SDK's ABI bindings (source/extensions/dynamic_modules/sdk/rust/src/abi.rs)
# from abi.h. The bindings are checked in: generating them in the SDK's build script made bindgen
# and its transitive crates a build dependency of the SDK, a serial chain of single-core compiles
# of around ten minutes on every cold CI leg. `:abi_bindings_test` (run by the format check)
# fails when the checked-in file no longer matches the header; `bazel run :fix` regenerates it.
rust_binary(
    name = "bindgen",
    srcs = ["src/main.rs"],
    edition = "2021",
    deps = crate_deps(["bindgen"]),
)

_HEADER = "//source/extensions/dynamic_modules/abi:abi.h"

_BINDINGS = "//source/extensions/dynamic_modules/sdk/rust:src/abi.rs"

# The hermetic clang: its libclang and the resource directory's builtin headers (stdbool.h and
# friends, which abi.h includes). The generator parses freestanding for a fixed target, so no
# sysroot is involved and the output is the same on any host.
_LINUX_ENV = {
    "LIBCLANG_PATH": "$(location @llvm_toolchain_llvm//:lib/libclang.so)",
    "ENVOY_LLVM_ANCHOR": "$(location @llvm_toolchain_llvm//:lib/libclang.so)",
    "ENVOY_LLVM_MAJOR": LLVM_MAJOR,
}

_LINUX_DATA = [
    "@llvm_toolchain_llvm//:lib/libclang.so",
    "@llvm_toolchain_llvm//:cxx_builtin_include",
]

_ENV = select({
    "@platforms//os:linux": _LINUX_ENV,
    "//conditions:default": {},
})

_DATA = [
    ":bindgen",
    _BINDINGS,
    _HEADER,
] + select({
    "@platforms//os:linux": _LINUX_DATA,
    "//conditions:default": [],
})

_WRAPPER_ENV = {
    "BINDGEN": "$(rootpath :bindgen)",
    "ABI_BINDINGS": "$(rootpath %s)" % _BINDINGS,
    "ABI_HEADER": "$(rootpath %s)" % _HEADER,
}

# Checked where the hermetic libclang is wired in (the format check runs on Linux); the
# generation itself is host-independent.
sh_test(
    name = "abi_bindings_test",
    size = "small",
    srcs = ["bindings.sh"],
    args = ["check"],
    data = _DATA,
    env = _WRAPPER_ENV | _ENV,
    target_compatible_with = ["@platforms//os:linux"],
)

sh_binary(
    name = "fix",
    srcs = ["bindings.sh"],
    args = ["fix"],
    data = _DATA,
    env = _WRAPPER_ENV | _ENV,
)

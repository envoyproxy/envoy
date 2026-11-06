load("@bazel_skylib//rules:common_settings.bzl", "string_flag")
load("@envoy_repo//:compiler.bzl", "LLVM_PATH")
load("@envoy_rust_crate_index//:defs.bzl", "all_crate_deps")
load("@rules_rust//cargo:defs.bzl", "cargo_build_script")
load("@rules_rust//rust:defs.bzl", "rust_library", "rust_static_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_extension_package",
)
load("//bazel:toolchains.bzl", "LLVM_MAJOR")
load(":symbol_prefix.bzl", "symbol_prefix_env")

licenses(["notice"])  # Apache 2

envoy_extension_package()

exports_files(["Cargo.toml"])

# Provides the Rust allocator shim (__rust_alloc etc) and std runtime exactly
# once in the final C++ link. All other in-binary Rust code must be rlibs
# (see envoy_rust_static_module in dynamic_modules.bzl).
rust_static_library(
    name = "rust_runtime",
    srcs = ["runtime.rs"],
    edition = "2021",
    visibility = ["//visibility:public"],
)

string_flag(
    name = "symbol_prefix",
    build_setting_default = "",
)

config_setting(
    name = "unprefixed",
    flag_values = {":symbol_prefix": ""},
)

symbol_prefix_env(
    name = "symbol_prefix_env",
    prefix = ":symbol_prefix",
)

cargo_build_script(
    name = "build_script",
    srcs = ["build.rs"],
    build_script_env = select({
        "@envoy_repo//:use_local_llvm": {
            "LIBCLANG_PATH": "%s/lib/libclang.so" % LLVM_PATH,
            "BINDGEN_EXTRA_CLANG_ARGS": " ".join([
                "-resource-dir %s/lib/clang/%s" % (LLVM_PATH, LLVM_MAJOR),
                "-isystem %s/lib/clang/%s/include" % (LLVM_PATH, LLVM_MAJOR),
                "-isystem %s/include/x86_64-unknown-linux-gnu/c++/v1/" % LLVM_PATH,
                "-isystem %s/include/aarch64-unknown-linux-gnu/c++/v1/" % LLVM_PATH,
            ]),
        },
        "@platforms//os:linux": {
            "LIBCLANG_PATH": "$(location @llvm_toolchain_llvm//:lib/libclang.so)",
            "BINDGEN_EXTRA_CLANG_ARGS": " ".join([
                "-resource-dir $${pwd}/external/llvm_toolchain_llvm/lib/clang/" + LLVM_MAJOR,
                "-isystem $${pwd}/external/llvm_toolchain_llvm/lib_legacy/clang/" + LLVM_MAJOR + "/include",
                "-isystem $${pwd}/external/llvm_toolchain_llvm/include/x86_64-unknown-linux-gnu/c++/v1/",
                "-isystem $${pwd}/external/llvm_toolchain_llvm/include/aarch64-unknown-linux-gnu/c++/v1/",
                # This allows the cross-compilation of the SDK to succeed by providing the necessary system headers for the target platform.
                "-isystem $${pwd}/external/sysroot_linux_x86_64/usr/include",
                "-isystem $${pwd}/external/sysroot_linux_x86_64/usr/include/x86_64-linux-gnu",
                "-isystem $${pwd}/external/sysroot_linux_arm64/usr/include",
                "-isystem $${pwd}/external/sysroot_linux_arm64/usr/include/aarch64-linux-gnu",
            ]),
        },
        "//conditions:default": {},
    }),
    build_script_env_files = [":symbol_prefix_env"],
    data = [
        ":abi/abi.h",
    ] + select({
        "@envoy_repo//:use_local_llvm": [],
        "@platforms//os:linux": [
            "@llvm_toolchain_llvm//:include",
            "@llvm_toolchain_llvm//:lib/libclang.so",
            "@llvm_toolchain_llvm//:lib_legacy",
        ],
        "//conditions:default": [],
    }) + select({
        # This allows the cross-compilation of the SDK to succeed by providing the necessary system headers for the target platform.
        "@platforms//cpu:x86_64": ["@sysroot_linux_amd64//:headers"],
        "@platforms//cpu:aarch64": ["@sysroot_linux_arm64//:headers"],
        "//conditions:default": [],
    }),
    edition = "2021",
    deps = all_crate_deps(
        build = True,
        normal = True,
    ),
)

rust_library(
    name = "envoy_proxy_dynamic_modules_rust_sdk",
    srcs = glob(["src/**/*.rs"]),
    # Generated compile data makes rules_rust rebase the crate root into the
    # configuration's output directory. Its crate hash must differ per prefix,
    # isolating Rust-mangled factory state as well as the exported C hooks.
    compile_data = [":symbol_prefix_env"],
    edition = "2021",
    deps = all_crate_deps(
        normal = True,
    ) + [":build_script"],
    alwayslink = select({
        ":unprefixed": False,
        "//conditions:default": True,
    }),
)

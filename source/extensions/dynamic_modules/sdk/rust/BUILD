load("@envoy_repo//:compiler.bzl", "LLVM_PATH")
load("@envoy_rust_crate_index//:defs.bzl", "all_crate_deps")
load("@rules_rust//cargo:defs.bzl", "cargo_build_script")
load("@rules_rust//rust:defs.bzl", "rust_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

exports_files(["Cargo.toml"])

cargo_build_script(
    name = "build_script",
    srcs = ["build.rs"],
    build_script_env = select({
        "@envoy_repo//:use_local_llvm": {
            "LIBCLANG_PATH": "%s/lib/libclang.so" % LLVM_PATH,
            "BINDGEN_EXTRA_CLANG_ARGS": " ".join([
                "-resource-dir %s/lib/clang/18" % LLVM_PATH,
                "-isystem %s/lib/clang/18/include" % LLVM_PATH,
                "-isystem %s/include/x86_64-unknown-linux-gnu/c++/v1/" % LLVM_PATH,
                "-isystem %s/include/aarch64-unknown-linux-gnu/c++/v1/" % LLVM_PATH,
            ]),
        },
        "@platforms//os:linux": {
            "LIBCLANG_PATH": "$(location @llvm_toolchain_llvm//:lib/libclang.so)",
            "BINDGEN_EXTRA_CLANG_ARGS": " ".join([
                "-resource-dir $${pwd}/external/llvm_toolchain_llvm/lib/clang/18",
                "-isystem $${pwd}/external/llvm_toolchain_llvm/lib_legacy/clang/18/include",
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
    data = [
        "//source/extensions/dynamic_modules/abi:abi.h",
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
    edition = "2021",
    deps = all_crate_deps(
        normal = True,
    ) + [":build_script"],
)

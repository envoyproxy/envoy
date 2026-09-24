load("@envoy_repo//:compiler.bzl", "LLVM_PATH")
load("@envoy_rust_crate_index//:defs.bzl", "all_crate_deps")
load("@rules_rust//cargo:defs.bzl", "cargo_build_script")
load("@rules_rust//rust:defs.bzl", "rust_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_extension_package",
)
load("//bazel:toolchains.bzl", "LLVM_MAJOR")

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
                "-resource-dir %s/lib/clang/%s" % (LLVM_PATH, LLVM_MAJOR),
                "-isystem %s/lib/clang/%s/include" % (LLVM_PATH, LLVM_MAJOR),
                "-isystem %s/include/x86_64-unknown-linux-gnu/c++/v1/" % LLVM_PATH,
                "-isystem %s/include/aarch64-unknown-linux-gnu/c++/v1/" % LLVM_PATH,
            ]),
        },
        "@platforms//os:linux": {
            "LIBCLANG_PATH": "$(location @llvm_toolchain_llvm//:lib/libclang.so)",
            "ENVOY_LLVM_ANCHOR": "$(location @llvm_toolchain_llvm//:lib/libclang.so)",
            "ENVOY_LLVM_MAJOR": LLVM_MAJOR,
            # This allows the cross-compilation of the SDK to succeed by providing the necessary system headers for the target platform.
            "ENVOY_SYSROOT_AMD64_ANCHOR": "$(location @sysroot_linux_amd64//:usr/include/stdio.h)",
            "ENVOY_SYSROOT_ARM64_ANCHOR": "$(location @sysroot_linux_arm64//:usr/include/stdio.h)",
        },
        "//conditions:default": {},
    }),
    data = [
        ":abi/abi.h",
    ] + select({
        "@envoy_repo//:use_local_llvm": [],
        "@platforms//os:linux": [
            "@llvm_toolchain_llvm//:include",
            "@llvm_toolchain_llvm//:lib/libclang.so",
            "@llvm_toolchain_llvm//:lib_legacy",
            "@sysroot_linux_amd64//:usr/include/stdio.h",
            "@sysroot_linux_arm64//:usr/include/stdio.h",
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

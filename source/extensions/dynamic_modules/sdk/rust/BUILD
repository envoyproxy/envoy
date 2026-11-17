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

# The LLVM distribution that bindgen runs: the host's, or for an arm64 build on arm64 workers from
# another host (`//bazel:arm64_workers`), the arm64 one.
LLVM_REPOS = [
    "llvm_toolchain_llvm",
    "llvm_toolchain_arm64_llvm",
]

BINDGEN_ENV = {
    llvm_repo: {
        "LIBCLANG_PATH": "$(location @%s//:lib/libclang.so)" % llvm_repo,
        "BINDGEN_EXTRA_CLANG_ARGS": " ".join([
            "-resource-dir $${pwd}/external/%s/lib/clang/%s" % (llvm_repo, LLVM_MAJOR),
            "-isystem $${pwd}/external/%s/lib_legacy/clang/%s/include" % (llvm_repo, LLVM_MAJOR),
            "-isystem $${pwd}/external/%s/include/x86_64-unknown-linux-gnu/c++/v1/" % llvm_repo,
            "-isystem $${pwd}/external/%s/include/aarch64-unknown-linux-gnu/c++/v1/" % llvm_repo,
            # This allows the cross-compilation of the SDK to succeed by providing the necessary system headers for the target platform.
            "-isystem $${pwd}/external/sysroot_linux_x86_64/usr/include",
            "-isystem $${pwd}/external/sysroot_linux_x86_64/usr/include/x86_64-linux-gnu",
            "-isystem $${pwd}/external/sysroot_linux_arm64/usr/include",
            "-isystem $${pwd}/external/sysroot_linux_arm64/usr/include/aarch64-linux-gnu",
        ]),
    }
    for llvm_repo in LLVM_REPOS
}

BINDGEN_DATA = {
    llvm_repo: [
        "@%s//:include" % llvm_repo,
        "@%s//:lib/libclang.so" % llvm_repo,
        "@%s//:lib_legacy" % llvm_repo,
    ]
    for llvm_repo in LLVM_REPOS
}

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
        "//bazel:arm64_workers": BINDGEN_ENV["llvm_toolchain_arm64_llvm"],
        "@platforms//os:linux": BINDGEN_ENV["llvm_toolchain_llvm"],
        "//conditions:default": {},
    }),
    data = [
        ":abi/abi.h",
    ] + select({
        "@envoy_repo//:use_local_llvm": [],
        "//bazel:arm64_workers": BINDGEN_DATA["llvm_toolchain_arm64_llvm"],
        "@platforms//os:linux": BINDGEN_DATA["llvm_toolchain_llvm"],
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

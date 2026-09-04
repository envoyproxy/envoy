load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("//bazel:toolchains.bzl", "LIBCLANG_CPP", "LIBLLVM")

licenses(["notice"])  # Apache 2

# Create a cc_library with LLVM headers
cc_library(
    name = "llvm_headers",
    hdrs = ["@llvm_toolchain_llvm//:all_includes"],
)

cc_binary(
    name = "prefixer",
    srcs = ["prefixer.cpp"],
    additional_linker_inputs = [
        LIBCLANG_CPP,
    ] + ([LIBLLVM] if LIBLLVM else []),
    copts = [
        "-fno-rtti",  # Required by libclang-cpp
        # Use libstdc++ for ABI compatibility with libclang-cpp
        "-stdlib=libstdc++",
        # -isystem paths for the LLVM headers and the libstdc++ headers from the
        # sysroot. These reference external repositories by their on-disk name,
        # which differs between WORKSPACE and bzlmod builds (bzlmod materialises
        # repos created by module extensions under their canonical name).
    ] + select({
        "//bazel:bzlmod_mode": [
            "-isystem external/envoy_toolshed++llvm_toolchain_alias_extension+llvm_toolchain_llvm/include",
            "-isystem external/envoy_toolshed++sysroot_extension+sysroot_linux_amd64/usr/include/c++/13",
            "-isystem external/envoy_toolshed++sysroot_extension+sysroot_linux_amd64/usr/include/x86_64-linux-gnu/c++/13",
        ],
        "//conditions:default": [
            "-isystem external/llvm_toolchain_llvm/include",
            "-isystem external/sysroot_linux_amd64/usr/include/c++/13",
            "-isystem external/sysroot_linux_amd64/usr/include/x86_64-linux-gnu/c++/13",
        ],
    }),
    linkopts = [
        "$(location " + LIBCLANG_CPP + ")",
    ] + (["$(location " + LIBLLVM + ")"] if LIBLLVM else []) + [
        "-Wl,-rpath,$$ORIGIN/../lib",
        # Use libstdc++ for ABI compatibility with libclang-cpp
        "-stdlib=libstdc++",
        # Explicitly link libstdc++ runtime (needed since toolchain defaults to libc++)
        "-lstdc++",
    ],
    visibility = ["//visibility:public"],
    deps = [":llvm_headers"],
)

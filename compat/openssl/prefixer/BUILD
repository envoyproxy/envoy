load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

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
        "@llvm_toolchain_llvm//:lib/libclang-cpp.so.18.1",
    ],
    copts = [
        "-fno-rtti",  # Required by libclang-cpp
        "-isystem external/llvm_toolchain_llvm/include",
        # Use libstdc++ for ABI compatibility with libclang-cpp
        "-stdlib=libstdc++",
        # Add libstdc++ include path from sysroot
        "-isystem external/sysroot_linux_amd64/usr/include/c++/13",
        "-isystem external/sysroot_linux_amd64/usr/include/x86_64-linux-gnu/c++/13",
    ],
    linkopts = [
        # Directly link to the versioned shared library
        "$(location @llvm_toolchain_llvm//:lib/libclang-cpp.so.18.1)",
        "-Wl,-rpath,$$ORIGIN/../lib",
        # Use libstdc++ for ABI compatibility with libclang-cpp
        "-stdlib=libstdc++",
        # Explicitly link libstdc++ runtime (needed since toolchain defaults to libc++)
        "-lstdc++",
    ],
    visibility = ["//visibility:public"],
    deps = [":llvm_headers"],
)

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("//bazel:toolchains.bzl", "LIBCLANG_CPP", "LIBLLVM")

licenses(["notice"])  # Apache 2

cc_library(
    name = "libstdcxx_headers",
    hdrs = ["@libstdcxx_sysroot_linux_amd64//:headers"],
    # TODO(phlax): replace with the hermetic gcc toolchain's libstdc++
)

cc_binary(
    name = "prefixer",
    srcs = ["prefixer.cpp"],
    additional_linker_inputs = [
        LIBCLANG_CPP,
    ] + ([LIBLLVM] if LIBLLVM else []) + [
        "@libstdcxx_sysroot_linux_amd64//:libs",
    ],
    copts = [
        "-fno-rtti",  # Required by libclang-cpp
        # Use libstdc++ for ABI compatibility with libclang-cpp
        "-stdlib=libstdc++",
        # libstdc++ headers come from a dedicated libstdc++-flavoured sysroot
        # (@libstdcxx_sysroot_linux_amd64); the main sysroot is libc-only.
        # TODO(phlax): source these from the hermetic gcc toolchain instead.
        "-isystem external/envoy_toolshed++sysroot_extension+libstdcxx_sysroot_linux_amd64/sysroot/usr/include/c++/13",
        "-isystem external/envoy_toolshed++sysroot_extension+libstdcxx_sysroot_linux_amd64/sysroot/usr/include/x86_64-linux-gnu/c++/13",
    ],
    linkopts = [
        "$(location " + LIBCLANG_CPP + ")",
    ] + (["$(location " + LIBLLVM + ")"] if LIBLLVM else []) + [
        "-Wl,-rpath,$$ORIGIN/../lib",
        # Use libstdc++ for ABI compatibility with libclang-cpp
        "-stdlib=libstdc++",
        # libstdc++ itself lives in the libstdc++ sysroot, not the main one. Only add the gcc dir:
        # adding usr/lib/x86_64-linux-gnu would also pull in that sysroot's glibc libs/linker scripts.
        "-Lexternal/envoy_toolshed++sysroot_extension+libstdcxx_sysroot_linux_amd64/sysroot/usr/lib/gcc/x86_64-linux-gnu/13",
        "-lstdc++",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":libstdcxx_headers",
        "@llvm_toolchain_llvm//:clang_tooling_headers",
    ],

)

load("@rules_cc//cc:defs.bzl", "cc_binary")
load("//bazel:toolchains.bzl", "LIBCLANG_CPP", "LIBLLVM")

licenses(["notice"])  # Apache 2

# GCC's libstdc++ for the prefixer, from a hermetic GCC repository that a host GCC override does
# not replace. It is available when Envoy is a dependency too; set this flag to use another.
label_flag(
    name = "libstdcxx",
    build_setting_default = "@gcc_runtime_linux_x86_64//:libstdcxx",
)

cc_binary(
    name = "prefixer",
    srcs = ["prefixer.cpp"],
    additional_linker_inputs = [
        LIBCLANG_CPP,
    ] + ([LIBLLVM] if LIBLLVM else []),
    copts = [
        "-fno-rtti",  # Required by libclang-cpp
        # Use the hermetic GCC toolchain's libstdc++ (headers via :libstdcxx) for ABI
        # compatibility with libclang-cpp, not the toolchain's libc++. With -nostdinc++
        # the toolchain's -stdlib=libc++ has no effect, so silence its warning.
        "-nostdinc++",
        "-Wno-unused-command-line-argument",
    ],
    linkopts = [
        "$(location " + LIBCLANG_CPP + ")",
    ] + (["$(location " + LIBLLVM + ")"] if LIBLLVM else []) + [
        "-Wl,-rpath,$$ORIGIN/../lib",
        # libstdc++.a comes from :libstdcxx (statically, so the tool does not need the host's
        # libstdc++.so.6); stop the driver adding its own C++ library.
        "-nostdlib++",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":libstdcxx",
        "@llvm_toolchain_llvm//:clang_tooling_headers",
    ],
)

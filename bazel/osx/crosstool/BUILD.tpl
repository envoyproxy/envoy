package(default_visibility = ["//visibility:public"])

load(":osx_archs.bzl", "OSX_TOOLS_ARCHS")

CC_TOOLCHAINS = [(
    cpu + "|compiler",
    ":cc-compiler-" + cpu,
) for cpu in OSX_TOOLS_ARCHS]

cc_library(
    name = "malloc",
)

cc_library(
    name = "stl",
)

filegroup(
    name = "empty",
    srcs = [],
)

filegroup(
    name = "cc_wrapper",
    srcs = ["cc_wrapper.sh"],
)

cc_toolchain_suite(
    name = "toolchain",
    toolchains = dict(CC_TOOLCHAINS),
)

[
    filegroup(
        name = "osx_tools_" + arch,
        srcs = [
          ":cc_wrapper",
          ":libtool",
          ":make_hashed_objlist.py",
          ":wrapped_clang",
          ":wrapped_clang_pp",
          ":wrapped_ar",
          ":xcrunwrapper.sh",
        ],
    )
    for arch in OSX_TOOLS_ARCHS
]

[
    apple_cc_toolchain(
        name = "cc-compiler-" + arch,
        all_files = ":osx_tools_" + arch,
        compiler_files = ":osx_tools_" + arch,
        cpu = arch,
        dwp_files = ":empty",
        dynamic_runtime_libs = [":empty"],
        linker_files = ":osx_tools_" + arch,
        objcopy_files = ":empty",
        static_runtime_libs = [":empty"],
        strip_files = ":osx_tools_" + arch,
        supports_param_files = 0,
    )
    for arch in OSX_TOOLS_ARCHS
]

load("@bazel_skylib//rules:copy_file.bzl", "copy_file")
load("@rules_cc//cc:defs.bzl", "cc_library")

# This declares a cc_library target that is used to build a shared library.
# name + ".c" is the source file that is compiled to create the shared library.
def test_program(name):
    cc_library(
        name = "_" + name,
        srcs = [name + ".c"],
        hdrs = [
            "//source/extensions/dynamic_modules:abi.h",
            "//source/extensions/dynamic_modules:abi_version.h",
        ],
        linkopts = [
            "-shared",
            "-fPIC",
        ],
        # All programs here are C, not C++, so we don't need to apply clang-tidy.
        tags = ["notidy"],
        linkstatic = False,
    )

    # Copy the shared library to the expected name especially for MacOS which
    # defaults to lib<name>.dylib.
    copy_file(
        name = name,
        src = "_" + name,
        out = "lib{}.so".format(name),
    )

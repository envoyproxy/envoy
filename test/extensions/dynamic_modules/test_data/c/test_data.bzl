load("@rules_cc//cc:defs.bzl", "cc_library", "cc_shared_library")

# This declares a cc_library target that is used to build a shared library.
# name + ".c" is the source file that is compiled to create the shared library.
def test_program(name):
    _name = "_" + name
    cc_library(
        name = _name,
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

    # Use cc_shared_library to create a shared library in a consistent naming across
    # platforms.
    cc_shared_library(
        name = name,
        shared_lib_name = "lib{}.so".format(name),
        deps = [_name],
    )

load("@rules_cc//cc:defs.bzl", "cc_library")

# This declares a cc_library target that is used to build a shared library.
# name + ".c" is the source file that is compiled to create the shared library.
def test_program(name):
    cc_library(
        name = name,
        srcs = [name + ".c"],
        linkopts = [
            "-shared",
            "-fPIC",
        ],
        linkstatic = False,
    )

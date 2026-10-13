load("@rules_cc//cc:defs.bzl", "cc_library", "cc_shared_library")

# This declares a cc_library target that is used to build a shared library.
# name + ".c" is the source file that is compiled to create the shared library.
def test_program(name):
    _name = "_" + name
    cc_library(
        name = _name,
        srcs = [name + ".cc"],
        deps = [
            "//source/extensions/dynamic_modules/sdk/cpp:sdk_abi",
        ],
        linkopts = [
            "-shared",
            "-fPIC",
        ],
        linkstatic = False,
    )

    # Use cc_shared_library to create a shared library in a consistent naming across
    # platforms.
    cc_shared_library(
        name = name,
        visibility = ["//visibility:public"],
        shared_lib_name = "lib{}.so".format(name),
        deps = [_name],
        user_link_flags = select({
            "//bazel:darwin_any": ["-Wl,-undefined,dynamic_lookup"],
            "//conditions:default": [],
        }),
    )

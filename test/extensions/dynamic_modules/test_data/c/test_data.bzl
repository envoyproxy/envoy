load("@rules_cc//cc:defs.bzl", "cc_library", "cc_shared_library")
load("//source/extensions/dynamic_modules:dynamic_modules.bzl", "envoy_dynamic_module_prefix_symbols")

# This declares a cc_library target that is used to build a shared library.
# name + ".c" is the source file that is compiled to create the shared library.
def test_program(name):
    _name = "_" + name
    cc_library(
        name = _name,
        srcs = [name + ".c"],
        deps = [
            "//source/extensions/dynamic_modules/abi",
        ],
        linkopts = select({
            "@platforms//os:macos": [
                "-shared",
                "-fPIC",
                "-undefined",
                "dynamic_lookup",
            ],
            "//conditions:default": [
                "-shared",
                "-fPIC",
            ],
        }),
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

    # Build static library with symbol prefixing for static linking into Envoy binary.
    # We compile the source once and create one renamed archive:
    #   With module_name=name → <name>_envoy_dynamic_module_on_*
    _static_lib_name = "_" + name + "_static_lib"
    cc_library(
        name = _static_lib_name,
        srcs = [name + ".c"],
        deps = [
            "//source/extensions/dynamic_modules/abi:abi",
        ],
        tags = ["notidy"],
    )

    envoy_dynamic_module_prefix_symbols(
        name = name + "_static",
        module_name = name + "_static",
        archive = ":" + _static_lib_name,
        tags = ["notidy"],
    )

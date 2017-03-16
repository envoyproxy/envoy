# References to Envoy external dependencies should be wrapped with this function.
def envoy_external_dep_path(dep):
    return "//external:%s" % dep

ENVOY_COPTS = [
    "-fno-omit-frame-pointer", "-Wall", "-Wextra", "-Werror", "-Wnon-virtual-dtor",
    "-Woverloaded-virtual", "-Wold-style-cast", "-std=c++0x",
    "-includesource/precompiled/precompiled.h", "-iquote", "include", "-iquote", "source"
]

# Envoy C++ library targets should be specified with this function.
def envoy_cc_library(name,
                     srcs = [],
                     public_hdrs = [],
                     hdrs = [],
                     external_deps = [],
                     deps = [],
                     copts = []):
    native.cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs + public_hdrs,
        deps = deps + [envoy_external_dep_path(dep) for dep in external_deps] +
               ["//source/precompiled:precompiled_includes"],
        copts = ENVOY_COPTS + copts,
    )

# Envoy C++ test targets should be specified with this function.
def envoy_cc_test(name,
                  srcs = [],
                  deps = []):
    native.cc_test(
        name = name,
        srcs = srcs,
        deps = deps + ["//source/precompiled:precompiled_includes",
                       "//test/precompiled:precompiled_includes"],
        copts = ENVOY_COPTS + ["-includetest/precompiled/precompiled_test.h"],
        linkopts = ["-pthread"]
    )

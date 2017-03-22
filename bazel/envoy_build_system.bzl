ENVOY_COPTS = [
    "-fno-omit-frame-pointer",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wnon-virtual-dtor",
    "-Woverloaded-virtual",
    "-Wold-style-cast",
    "-std=c++0x",
    "-includeprecompiled/precompiled.h",
]

# References to Envoy external dependencies should be wrapped with this function.
def envoy_external_dep_path(dep):
    return "//external:%s" % dep

# Transform the package path (e.g. include/envoy/common) into a path for
# exporting the package headers at (e.g. envoy/common). Source files can then
# include using this path scheme (e.g. #include "envoy/common/time.h").
def envoy_include_prefix(path):
  if path.startswith('source/') or path.startswith('include/'):
    return '/'.join(path.split('/')[1:])
  return path

# Envoy C++ library targets should be specified with this function.
def envoy_cc_library(name,
                     srcs = [],
                     hdrs = [],
                     public_hdrs = [],
                     copts = [],
                     alwayslink = None,
                     visibility = None,
                     external_deps = [],
                     deps = []):
    native.cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs + public_hdrs,
        copts = ENVOY_COPTS + copts,
        visibility = visibility,
        deps = deps + [envoy_external_dep_path(dep) for dep in external_deps] + [
            "//source/precompiled:precompiled_includes",
        ],
        alwayslink = alwayslink,
        include_prefix = envoy_include_prefix(PACKAGE_NAME),
    )

# Envoy C++ test targets should be specified with this function.
def envoy_cc_test(name,
                  srcs = [],
                  deps = []):
    native.cc_test(
        name = name,
        srcs = srcs,
        copts = ENVOY_COPTS + ["-includetest/precompiled/precompiled_test.h"],
        linkopts = ["-pthread"],
        deps = deps + [
            "//source/precompiled:precompiled_includes",
            "//test/precompiled:precompiled_includes",
        ],
    )

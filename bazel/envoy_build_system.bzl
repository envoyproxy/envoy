load("@protobuf_git//:protobuf.bzl", "cc_proto_library")

ENVOY_COPTS = [
    # TODO(htuch): Remove this when Bazel bringup is done.
    "-DBAZEL_BRINGUP",
    "-fno-omit-frame-pointer",
    # TODO(htuch): Clang wants -ferror-limit, should support both. Commented out for now.
    # "-fmax-errors=3",
    "-Wall",
    # TODO(htuch): Figure out why protobuf-3.2.0 causes the CI build to fail
    # with this but not the developer-local build.
    #"-Wextra",
    "-Werror",
    "-Wnon-virtual-dtor",
    "-Woverloaded-virtual",
    # TODO(htuch): Figure out how to use this in the presence of headers in
    # openssl/tclap which use old style casts.
    # "-Wold-style-cast",
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
    return None

# Envoy C++ library targets should be specified with this function.
def envoy_cc_library(name,
                     srcs = [],
                     hdrs = [],
                     copts = [],
                     visibility = None,
                     external_deps = [],
                     deps = [],
                     alwayslink = None):
    native.cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        copts = ENVOY_COPTS + copts,
        visibility = visibility,
        deps = deps + [envoy_external_dep_path(dep) for dep in external_deps] + [
            "//include/envoy/common:base_includes",
            "//source/precompiled:precompiled_includes",
        ],
        include_prefix = envoy_include_prefix(PACKAGE_NAME),
        alwayslink = alwayslink,
    )

# Envoy C++ binary targets should be specified with this function.
def envoy_cc_binary(name,
                    srcs = [],
                    data = [],
                    testonly = 0,
                    visibility = None,
                    deps = []):
    native.cc_binary(
        name = name,
        srcs = srcs,
        data = data,
        copts = ENVOY_COPTS,
        linkopts = [
            "-pthread",
            "-lrt",
        ],
        testonly = testonly,
        linkstatic = 1,
        visibility = visibility,
        deps = deps + [
            "//source/precompiled:precompiled_includes",
        ],
    )

# Envoy C++ test targets should be specified with this function.
def envoy_cc_test(name,
                  srcs = [],
                  data = [],
                  args = [],
                  # List of pairs (Bazel shell script target, shell script args)
                  setup_cmds = [],
                  deps = []):
    if setup_cmds:
        setup_cmd = "; ".join(["$(location " + setup_sh + ") " + " ".join(setup_args) for
                               setup_sh, setup_args in setup_cmds])
        envoy_test_setup_flag = "--envoy_test_setup=\"" + setup_cmd + "\""
        data += [setup_sh for setup_sh, _ in setup_cmds]
        args += [envoy_test_setup_flag]
    native.cc_test(
        name = name,
        srcs = srcs,
        data = data,
        copts = ENVOY_COPTS + ["-includetest/precompiled/precompiled_test.h"],
        linkopts = ["-pthread"],
        linkstatic = 1,
        args = args,
        deps = deps + [
            "//source/precompiled:precompiled_includes",
            "//test/precompiled:precompiled_includes",
            "//test:main",
        ],
    )

# Envoy C++ test targets that depend on JSON config files subject to template
# environment substitution should be specified with this function.
def envoy_cc_test_with_json(name,
                            srcs = [],
                            data = [],
                            jsons = [],
                            setup_cmds = [],
                            deps = []):
    envoy_cc_test(
        name = name,
        srcs = srcs,
        data = data + jsons,
        setup_cmds = setup_cmds + [(
            "//test/test_common:environment_sub.sh",
            ["$(location " + f + ")" for f in jsons],
        )],
        deps = deps,
    )

# Envoy C++ test related libraries (that want gtest, gmock) should be specified
# with this function.
def envoy_cc_test_library(name,
                          srcs = [],
                          hdrs = [],
                          data = [],
                          external_deps = [],
                          deps = [],
                          alwayslink = None):
    native.cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        data = data,
        copts = ENVOY_COPTS + ["-includetest/precompiled/precompiled_test.h"],
        testonly = 1,
        deps = deps + [envoy_external_dep_path(dep) for dep in external_deps] + [
            "//source/precompiled:precompiled_includes",
            "//test/precompiled:precompiled_includes",
        ],
        alwayslink = alwayslink,
    )

# Envoy C++ mock targets should be specified with this function.
def envoy_cc_mock(name,
                  srcs = [],
                  hdrs = [],
                  deps = []):
    envoy_cc_test_library(name = name,
                          srcs = srcs,
                          hdrs = hdrs,
                          deps = deps)

def _proto_header(proto_path):
  if proto_path.endswith(".proto"):
      return proto_path[:-5] + "pb.h"
  return None

# Envoy proto targets should be specified with this function.
def envoy_proto_library(name, srcs = [], deps = []):
    internal_name = name + "_internal"
    cc_proto_library(
        name = internal_name,
        srcs = srcs,
        default_runtime = "//external:protobuf",
        protoc = "//external:protoc",
        deps = deps,
    )
    # We can't use include_prefix directly in cc_proto_library, since it
    # confuses protoc. Instead, we create a shim cc_library that performs the
    # remap of .pb.h location to Envoy canonical header paths.
    native.cc_library(
        name = name,
        hdrs = [_proto_header(s) for s in srcs if _proto_header(s)],
        include_prefix = envoy_include_prefix(PACKAGE_NAME),
        deps = [internal_name],
    )

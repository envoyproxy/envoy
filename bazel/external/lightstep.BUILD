load("@envoy//bazel:genrule_repository.bzl", "genrule_cc_deps", "genrule_environment")
load(":genrule_cmd.bzl", "genrule_cmd")

cc_library(
    name = "lightstep",
    srcs = [":_prefix/lib/liblightstep_core_cxx11.a"],
    hdrs = glob([
        "src/c++11/lightstep/**/*.h",
        "src/c++11/mapbox_variant/**/*.hpp",
    ]) + [
        ":_prefix/include/collector.pb.h",
        ":_prefix/include/lightstep_carrier.pb.h",
    ],
    includes = [
        "_prefix/include",
        "src/c++11",
    ],
    visibility = ["//visibility:public"],
    deps = ["@protobuf_bzl//:protobuf"],
)

genrule_environment(
    name = "lightstep_compiler_flags",
)

# This intermediate rule lets cc_library outputs be fed into a genrule.
# Normally a cc_library creates a CcSkylarkApiProvider, but no direct
# file outputs.
genrule_cc_deps(
    name = "protobuf_deps",
    deps = ["@protobuf_bzl//:protobuf"],
)

genrule(
    name = "install",
    srcs = glob(["**"]) + [
        ":lightstep_compiler_flags",
        ":protobuf_deps",
        "@protobuf_bzl//:well_known_protos",
    ],
    outs = [
        "_prefix/lib/liblightstep_core_cxx11.a",
        "_prefix/include/collector.pb.h",
        "_prefix/include/lightstep_carrier.pb.h",
    ],
    cmd = genrule_cmd("@envoy//bazel/external:lightstep.genrule_cmd"),
    tools = [
        "@protobuf_bzl//:protoc",
    ],
)

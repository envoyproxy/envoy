load("@envoy//bazel:genrule_repository.bzl", "genrule_cc_deps", "genrule_environment")
load(":genrule_cmd.bzl", "genrule_cmd")

_HDRS = glob([
    "src/c++11/lightstep/**/*.h",
    "src/c++11/mapbox_variant/**/*.hpp",
])

_PREFIX_HDRS = [hdr.replace("src/c++11/", "_prefix/include/") for hdr in _HDRS] + [
    "_prefix/include/collector.pb.h",
    "_prefix/include/lightstep_carrier.pb.h",
]

cc_library(
    name = "lightstep",
    srcs = [":_prefix/lib/liblightstep_core_cxx11.a"],
    hdrs = [":" + hdr for hdr in _PREFIX_HDRS],
    includes = ["_prefix/include"],
    visibility = ["//visibility:public"],
    deps = ["@com_google_protobuf//:protobuf"],
)

genrule_environment(
    name = "lightstep_compiler_flags",
)

# This intermediate rule lets cc_library outputs be fed into a genrule.
# Normally a cc_library creates a CcSkylarkApiProvider, but no direct
# file outputs.
genrule_cc_deps(
    name = "protobuf_deps",
    deps = ["@com_google_protobuf//:protobuf"],
)

genrule(
    name = "install",
    srcs = glob(["**"]) + [
        ":lightstep_compiler_flags",
        ":protobuf_deps",
        "@com_google_protobuf//:well_known_protos",
        "@local_config_cc//:toolchain",
    ],
    outs = _PREFIX_HDRS + [
        "_prefix/lib/liblightstep_core_cxx11.a",
    ],
    cmd = genrule_cmd("@envoy//bazel/external:lightstep.genrule_cmd"),
    tools = [
        "@com_google_protobuf//:protoc",
    ],
)

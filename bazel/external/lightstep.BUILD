load("@envoy//bazel:genrule_repository.bzl", "genrule_cc_deps", "genrule_environment")
load(":genrule_cmd.bzl", "genrule_cmd")

_HDRS = glob([
    "include/lightstep/**/*.h",
])

_PREFIX_HDRS = ["_prefix/" + hdr for hdr in _HDRS] + [
    "_prefix/include/lightstep/version.h",
    "_prefix/include/lightstep/config.h",
]

cc_library(
    name = "lightstep",
    srcs = [":_prefix/lib/liblightstep_tracer.a"],
    hdrs = [":" + hdr for hdr in _PREFIX_HDRS],
    includes = ["_prefix/include"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_protobuf//:protobuf",
        "@com_github_opentracing_opentracing_cpp//:opentracing",
    ],
)

genrule_environment(
    name = "lightstep_compiler_flags",
)

# These intermediate rules let cc_library outputs be fed into a genrule.
# Normally a cc_library creates a CcSkylarkApiProvider, but no direct
# file outputs.
genrule_cc_deps(
    name = "protobuf_deps",
    deps = ["@com_google_protobuf//:protobuf"],
)

genrule_cc_deps(
    name = "opentracing_deps",
    deps = ["@com_github_opentracing_opentracing_cpp//:opentracing"],
)


genrule(
    name = "install",
    srcs = glob(["**"]) + [
        ":lightstep_compiler_flags",
        ":protobuf_deps",
        ":opentracing_deps",
        "@com_github_opentracing_opentracing_cpp//:opentracing_library",
        "@com_google_protobuf//:well_known_protos",
        "@local_config_cc//:toolchain",
    ],
    outs = _PREFIX_HDRS + [
        "_prefix/lib/liblightstep_tracer.a",
    ],
    cmd = genrule_cmd("@envoy//bazel/external:lightstep.genrule_cmd"),
    tools = [
        "@com_google_protobuf//:protoc",
    ],
)

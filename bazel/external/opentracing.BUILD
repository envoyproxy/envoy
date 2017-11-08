load("@envoy//bazel:genrule_repository.bzl", "genrule_cc_deps", "genrule_environment")
load(":genrule_cmd.bzl", "genrule_cmd")

_HDRS = glob([
    "include/opentracing/**/*.h",
    "include/opentracing/**/*.hpp",
])

_THIRD_PARTY_HDRS = glob([
    "3rd_party/include/opentracing/**/*.h",
    "3rd_party/include/opentracing/**/*.hpp",
])

_PREFIX_HDRS = ["_prefix/" + hdr for hdr in _HDRS] + [
  hdr.replace("3rd_party/", "_prefix/") for hdr in _THIRD_PARTY_HDRS] + [
    "_prefix/include/opentracing/version.h",
]

cc_library(
    name = "opentracing",
    srcs = [":_prefix/lib/libopentracing.a"],
    hdrs = [":" + hdr for hdr in _PREFIX_HDRS],
    includes = ["_prefix/include"],
    visibility = ["//visibility:public"],
)

genrule_environment(
    name = "opentracing_compiler_flags",
)

filegroup(
    name = "opentracing_library",
    srcs = [":_prefix/lib/libopentracing.a"],
    visibility = ["//visibility:public"],
)

genrule(
    name = "install",
    srcs = glob(["**"]) + [
        ":opentracing_compiler_flags",
        "@local_config_cc//:toolchain",
    ],
    outs = _PREFIX_HDRS + [
        "_prefix/lib/libopentracing.a",
    ],
    cmd = genrule_cmd("@envoy//bazel/external:opentracing.genrule_cmd"),
)

load("@rules_cc//cc:defs.bzl", "cc_library")
load(":genrule_cmd.bzl", "genrule_cmd")

licenses(["notice"])  # Apache 2

cc_library(
    name = "vpp_vcl",
    srcs = ["libvppcom.so"],
    hdrs = ["src/vcl/vppcom.h"],
    defines = ["VPP_VCL"],
    includes = ["src/vcl/"],
    tags = ["skip_on_windows"],
    visibility = ["//visibility:public"],
)

genrule(
    name = "build",
    srcs = glob(["**"]),
    outs = [
        "libvppcom.so",
    ],
    cmd = genrule_cmd("@envoy//bazel/external:vpp_vcl.genrule_cmd"),
)

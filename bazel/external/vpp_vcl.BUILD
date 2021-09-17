load("@rules_cc//cc:defs.bzl", "cc_library")
load(":genrule_cmd.bzl", "genrule_cmd")

licenses(["notice"])  # Apache 2

cc_library(
    name = "vpp_vcl",
    srcs = [
        "libsvm.a",
        "libvlibmemoryclient.a",
        "libvppcom.a",
        "libvppinfra.a",
    ],
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
        "libvppcom.a",
        "libvppinfra.a",
        "libsvm.a",
        "libvlibmemoryclient.a",
    ],
    cmd = genrule_cmd("@envoy//bazel/external:vpp_vcl.genrule_cmd"),
)

package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

genrule(
    name = "envoy_version",
    srcs = glob([".git/**"]),
    outs = ["version_generated.cc"],
    cmd = "touch $@ && $(location //tools:gen_git_sha.sh) " +
          "$$(dirname $(location //tools:gen_git_sha.sh)) $@",
    local = 1,
    tools = ["//tools:gen_git_sha.sh"],
)

envoy_cc_library(
    name = "version_generated",
    srcs = ["version_generated.cc"],
    deps = ["//source/common/common:version_includes"],
)

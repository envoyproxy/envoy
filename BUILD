load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

envoy_package()

genrule(
    name = "envoy_version",
    srcs = glob([".git/**"]),
    outs = ["version_generated.cc"],
    cmd = "touch $@ && $(location //tools:gen_git_sha.sh) " +
          "$$(dirname $(location //tools:gen_git_sha.sh)) $@",
    local = 1,
    tools = ["//tools:gen_git_sha.sh"],
)

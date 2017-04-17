package(default_visibility = ["//visibility:public"])

genrule(
    name = "envoy_version",
    srcs = glob([".git/**"]),
    outs = ["version_generated.cc"],
    cmd = "touch $@ && $(location //tools:gen_git_sha.sh) " +
          "$$(dirname $(location //tools:gen_git_sha.sh)) $@",
    local = 1,
    tools = ["//tools:gen_git_sha.sh"],
)

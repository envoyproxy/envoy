workspace(name = "envoy")

load("//bazel:repositories.bzl", "envoy_dependencies")

envoy_dependencies()

bind(
    name = "googletest_main",
    actual = "@googletest//:googletest_main",
)

bind(
    name = "spdlog",
    actual = "@spdlog_git//:spdlog",
)

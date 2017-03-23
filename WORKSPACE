workspace(name = "envoy")

load("//bazel:repositories.bzl", "envoy_dependencies")

envoy_dependencies()

bind(
    name = "ares",
    actual = "@cares_git//:ares",
)

bind(
    name = "event",
    actual = "@libevent_git//:event",
)

bind(
    name = "event_pthreads",
    actual = "@libevent_git//:event_pthreads",
)

bind(
    name = "googletest",
    actual = "@googletest_git//:googletest",
)

bind(
    name = "spdlog",
    actual = "@spdlog_git//:spdlog",
)

bind(
    name = "ssl",
    actual = "@boringssl//:ssl",
)

bind(
    name = "tclap",
    actual = "@tclap_archive//:tclap",
)

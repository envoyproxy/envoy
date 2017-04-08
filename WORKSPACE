workspace(name = "envoy")

load("//bazel:repositories.bzl", "envoy_dependencies")

envoy_dependencies()

bind(
    name = "ares",
    actual = "@cares_git//:ares",
)

bind(
    name = "cc_wkt_protos_genproto",
    actual = "@protobuf_git//:cc_wkt_protos_genproto",
)

bind(
    name = "cc_wkt_protos",
    actual = "@protobuf_git//:cc_wkt_protos",
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
    name = "http_parser",
    actual = "@http_parser_git//:http_parser",
)

bind(
    name = "lightstep",
    actual = "@lightstep_git//:lightstep_core",
)

bind(
    name = "nghttp2",
    actual = "@nghttp2_tar//:nghttp2",
)

bind(
    name = "protobuf",
    actual = "@protobuf_git//:protobuf",
)

bind(
    name = "protoc",
    actual = "@protobuf_git//:protoc",
)

bind(
    name = "rapidjson",
    actual = "@rapidjson_git//:rapidjson",
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

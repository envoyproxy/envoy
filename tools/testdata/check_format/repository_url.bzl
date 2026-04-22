load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "foo",
    url = "http://foo.com",
    sha256 = "blah",
)

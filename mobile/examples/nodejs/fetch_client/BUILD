licenses(["notice"])  # Apache 2

sh_binary(
    name = "fetch_client",
    srcs = ["//test/nodejs:run_node_test.sh"],
    args = ["examples/nodejs/fetch_client/fetch_client.js"],
    data = [
        "fetch_client.js",
        "//library/nodejs:library",
    ],
)

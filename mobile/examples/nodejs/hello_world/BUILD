licenses(["notice"])  # Apache 2

sh_binary(
    name = "hello_world",
    srcs = ["//test/nodejs:run_node_test.sh"],
    args = ["examples/nodejs/hello_world/hello_world.js"],
    data = [
        "hello_world.js",
        "//library/nodejs:library",
    ],
)

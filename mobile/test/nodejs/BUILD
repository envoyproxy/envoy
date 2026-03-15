licenses(["notice"])  # Apache 2

exports_files(["run_node_test.sh"])

sh_test(
    name = "test_binding",
    srcs = ["run_node_test.sh"],
    args = ["test/nodejs/test_binding.js"],
    data = [
        "test_binding.js",
        "//library/nodejs:library",
    ],
    local = True,
)

sh_test(
    name = "test_envoy_client",
    srcs = ["run_node_test.sh"],
    args = ["test/nodejs/test_envoy_client.js"],
    data = [
        "test_envoy_client.js",
        "//library/nodejs:library",
    ],
    local = True,
)

load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

genrule(
    name = "configs",
    srcs = [
        "//configs",
        "@envoy-examples//:configs",
        "@envoy-examples//:certs",
        "@envoy-examples//:lua",
        # TODO(phlax): re-enable once wasm example is fixed
        # "@envoy-examples//wasm-cc:configs",
        "//docs:configs",
        "//docs:proto_examples",
        "//test/config/integration/certs",
    ],
    outs = ["configs.tar"],
    cmd = """
    $(location //configs:configgen.sh) $(location //configs:configgen) $@ $(@D) \
        $(locations //configs)  \
        $(locations @envoy-examples//:configs)  \
        $(locations @envoy-examples//:certs)  \
        $(locations @envoy-examples//:lua)  \
        $(locations //docs:configs)  \
        $(locations //docs:proto_examples)  \
        $(locations //test/config/integration/certs)
    """,
    # "$(locations @envoy-examples//wasm-cc:configs) " \
    tools = [
        "//configs:configgen",
        "//configs:configgen.sh",
    ],
)

genrule(
    name = "contrib_configs",
    srcs = [
        "//contrib:config_data",
        "//docs:contrib_configs",
        "@envoy-examples//:contrib_configs",
        "@envoy-examples//:certs",
        "//test/config/integration/certs",
    ],
    outs = ["contrib_configs.tar"],
    cmd = """
    $(location //configs:configgen.sh) $(location //configs:configgen) $@ $(@D) \
        $(locations //docs:contrib_configs) \
        $(locations //contrib:config_data) \
        $(locations //test/config/integration/certs) \
        $(locations @envoy-examples//:contrib_configs) \
        $(locations @envoy-examples//:certs)
    """,
    tools = [
        "//configs:configgen",
        "//configs:configgen.sh",
    ],
)

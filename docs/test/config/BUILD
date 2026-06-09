load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

genrule(
    name = "configs",
    srcs = [
        "@envoy//configs",
        "@envoy-examples//:configs",
        "@envoy-examples//:certs",
        "@envoy-examples//:lua",
        # TODO(phlax): re-enable once wasm example is fixed
        # "@envoy-examples//wasm-cc:configs",
        "//:configs",
        "//:proto_examples",
        "@envoy//test/config/integration/certs",
    ],
    outs = ["configs.tar"],
    cmd = """
    $(location @envoy//configs:configgen.sh) $(location @envoy//configs:configgen) $@ $(@D) \
        $(locations @envoy//configs)  \
        $(locations @envoy-examples//:configs)  \
        $(locations @envoy-examples//:certs)  \
        $(locations @envoy-examples//:lua)  \
        $(locations //:configs)  \
        $(locations //:proto_examples)  \
        $(locations @envoy//test/config/integration/certs)
    """,
    # "$(locations @envoy-examples//wasm-cc:configs) " \
    tools = [
        "@envoy//configs:configgen",
        "@envoy//configs:configgen.sh",
    ],
)

genrule(
    name = "contrib_configs",
    srcs = [
        "@envoy//contrib:config_data",
        "//:contrib_configs",
        "@envoy-examples//:contrib_configs",
        "@envoy-examples//:certs",
        "@envoy//test/config/integration/certs",
    ],
    outs = ["contrib_configs.tar"],
    cmd = """
    $(location @envoy//configs:configgen.sh) $(location @envoy//configs:configgen) $@ $(@D) \
        $(locations //:contrib_configs) \
        $(locations @envoy//contrib:config_data) \
        $(locations @envoy//test/config/integration/certs) \
        $(locations @envoy-examples//:contrib_configs) \
        $(locations @envoy-examples//:certs)
    """,
    tools = [
        "@envoy//configs:configgen",
        "@envoy//configs:configgen.sh",
    ],
)

load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

genquery(
    name = "active_proto_targets",
    expression = "labels(srcs, labels(deps, @envoy_api//versioning:active_protos))",
    scope = ["@envoy_api//versioning:active_protos"],
)

genquery(
    name = "frozen_proto_targets",
    expression = "labels(srcs, labels(deps, @envoy_api//versioning:frozen_protos))",
    scope = ["@envoy_api//versioning:frozen_protos"],
)

genrule(
    name = "proto_targets",
    outs = ["proto_targets.txt"],
    cmd = """
    cat $(location :active_proto_targets) \
        $(location :frozen_proto_targets) \
        > $@
    """,
    tools = [
        ":active_proto_targets",
        ":frozen_proto_targets",
    ],
)

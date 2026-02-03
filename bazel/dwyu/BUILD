load("@depend_on_what_you_use//:defs.bzl", "MAP_TRANSITIVE_DEPS", "dwyu_make_cc_info_mapping")

licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

dwyu_make_cc_info_mapping(
    name = "map_transitive_deps",
    mapping = {
        "@envoy_api//envoy/extensions/common/aws/v3:pkg_cc_proto": MAP_TRANSITIVE_DEPS,
    },
)

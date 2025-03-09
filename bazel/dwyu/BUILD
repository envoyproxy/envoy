load("@depend_on_what_you_use//:defs.bzl", "MAP_TRANSITIVE_DEPS", "dwyu_make_cc_info_mapping")
package(default_visibility = ["//visibility:public"])
dwyu_make_cc_info_mapping(
    name = "map_transitive_deps",
    mapping = {
        "//dwyu-test:addressbook_proto" : MAP_TRANSITIVE_DEPS
    },
)

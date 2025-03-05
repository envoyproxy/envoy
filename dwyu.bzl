load("@depend_on_what_you_use//:defs.bzl","MAP_DIRECT_DEPS", "MAP_TRANSITIVE_DEPS", "dwyu_make_cc_info_mapping", "dwyu_aspect_factory")

# dwyu = dwyu_aspect_factory(skipped_tags = ["nocompdb","api_proto_package"], skip_external_targets = True, verbose = True, ignored_includes = Label("//:ignore_includes.json"))
dwyu = dwyu_aspect_factory(skipped_tags = ["nocompdb","api_proto_package"], skip_external_targets = True, verbose = True, target_mapping = Label("@//:map_transitive_deps"))
# dwyu = dwyu_aspect_factory(skipped_tags = ["nocompdb"], verbose = True, ignored_includes = Label("//:ignore_includes.json"));


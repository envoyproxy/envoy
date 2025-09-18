load("@depend_on_what_you_use//:defs.bzl", "dwyu_aspect_factory")

dwyu = dwyu_aspect_factory(skipped_tags = ["nocompdb"], target_mapping = Label("//bazel/dwyu:map_transitive_deps"))

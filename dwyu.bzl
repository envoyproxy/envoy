load("@depend_on_what_you_use//:defs.bzl", "dwyu_aspect_factory")

dwyu = dwyu_aspect_factory(skipped_tags = ["nocompdb"], experimental_set_cplusplus = True)

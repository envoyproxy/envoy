load("@depend_on_what_you_use//:defs.bzl", "dwyu_aspect_factory")

dwyu = dwyu_aspect_factory(skipped_tags = ["nocompdb"], verbose = True, ignored_includes = Label("//:ignore_includes.json"))

load("@envoy_toolshed//dependency:macros.bzl", "module_deps_json", "module_updater", "registry_updater")

licenses(["notice"])  # Apache 2

module_deps_json(
    name = "module_deps",
    lockfile = "//:MODULE.bazel.lock",
    module_file = "//:MODULE.bazel",
)

module_updater(
    name = "update_module",
    bazelrc = "//:.bazelrc",
    dependencies = ":module_deps",
    module_file = "//:MODULE.bazel",
)

registry_updater(
    name = "update_registry",
    bazelrc = "//:.bazelrc",
)

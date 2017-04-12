# Adding external dependencies to Envoy

1. Specify name and version in [external documentation](../docs/install/requirements.rst).
2. Add a build recipe in [`ci/build_container/build_recipes`](../ci/build_container/build_recipes)
   for developer-local and CI external dependency build flows.
3. Add the build recipe to the list in [`bazel/recipes.bzl`](recipes.bzl).
4. Add a build target X in [`ci/prebuilt/BUILD`](../ci/prebuilt/BUILD) to consume the headers and
   libraries.
5. Add a bind target in [`ci/repositories.bzl`](repositories.bzl#L72) to allow the new dependency to be
   consumed by WORKSPACE.
6. Reference your new external dependency in some `envoy_cc_library` via X in the `external_deps`
   attribute.
7. `bazel test //test/...`

# Updating an external dependency version

1. Specify the new version in [external documentation](../docs/install/requirements.rst).
2. Update the build recipe in [`ci/build_container/build_recipes`](../ci/build_container/build_recipes).
3. `bazel test //test/...`

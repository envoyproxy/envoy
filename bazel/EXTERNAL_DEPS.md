# Adding external dependencies to Envoy

1. Specify name and version in [external documentation](../docs/install/requirements.rst).
2. Add a build recipe X in [`ci/build_container/build_recipes`](../ci/build_container/build_recipes)
   for developer-local and CI external dependency build flows.
3. Add a build target Y in [`ci/prebuilt/BUILD`](../ci/prebuilt/BUILD) to consume the headers and
   libraries produced by the build recipe X.
4. Add a map from target Y to build recipe X in [`target_recipes.bzl`](target_recipes.bzl).
5. Reference your new external dependency in some `envoy_cc_library` via Y in the `external_deps`
   attribute.
6. `bazel test //test/...`

# Updating an external dependency version

1. Specify the new version in [external documentation](../docs/install/requirements.rst).
2. Update the build recipe in [`ci/build_container/build_recipes`](../ci/build_container/build_recipes).
3. `bazel test //test/...`

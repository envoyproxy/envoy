# Adding external dependencies to Envoy

1. Specify name and version in [external documentation](../docs/install/requirements.rst).
2. Add a build recipe in [`ci/build_container/build_recipes`](../ci/build_container/build_recipes)
   for developer-local and CI external dependency build flows. Reference build recipe from the [`Makefile`](../ci/build_container/Makefile).
3. Verify that the build recipe works by running `ENVOY_SRC_DIR=$PWD tools/setup_external_deps.sh`
   from the Envoy root. The built artifacts are located in `build/prebuilt`.
4. Add a `LIBS` entry in `bazel/gen_prebuilt.py` providing a reference to the build recipe and
   a name X for the new external dependency target.
5. `bazel/gen_prebuilt.sh` to rebuild `bazel/prebuilt.bzl`.
6. Reference your new external dependency in some `envoy_cc_library` via X in the `external_deps`
   attribute.
7. `bazel test //test/...`

# Updating an external dependency version

1. Specify the new version in [external documentation](../docs/install/requirements.rst).
2. Update the build recipe in [`ci/build_container/build_recipes`](../ci/build_container/build_recipes).
3. `bazel/gen_prebuilt.sh` to rebuild `bazel/prebuilt.bzl`.
4. `bazel test //test/...`

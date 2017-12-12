# Adding external dependencies to Envoy (native Bazel)

This is the preferred style of adding dependencies that use Bazel for their
build process.

1. Define a new Bazel repository in [`bazel/repositories.bzl`](repositories.bzl),
   in the `envoy_dependencies()` function.
2. Reference your new external dependency in some `envoy_cc_library` via the
   `external_deps` attribute.
3. `bazel test //test/...`

# Adding external dependencies to Envoy (genrule repository)

This is the newer style of adding dependencies with no upstream Bazel configs.
It wraps the dependency's native build tooling in a Bazel-aware shell script,
installing to a Bazel-managed prefix.

The shell script is executed by Bash, with a few Bazel-specific extensions.
See the [Bazel docs for "genrule"](https://docs.bazel.build/versions/master/be/general.html#genrule)
for details on Bazel's shell extensions.

1. Add a BUILD file in [`bazel/external/`](external/), using a `genrule` target
   to build the dependency. Please do not add BUILD logic that replaces the
   dependency's upstream build tooling.
2. Define a new Bazel repository in [`bazel/repositories.bzl`](repositories.bzl),
   in the `envoy_dependencies()` function. The repository may use `genrule_repository`
   from [`bazel/genrule_repository.bzl`](genrule_repository.bzl) to place large
   genrule shell commands into a separate file.
3. Reference your new external dependency in some `envoy_cc_library` via Y in the
   `external_deps` attribute.
4. `bazel test //test/...`

Dependencies between external libraries can use the standard Bazel dependency
resolution logic, using the `$(location)` shell extension to resolve paths
to binaries, libraries, headers, etc. See
[`bazel/external/lightstep.genrule_cmd`](external/lightstep.genrule_cmd) for
an example.

# Adding external dependencies to Envoy (build recipe)

This is the older style of adding dependencies. It uses shell scripts to build
and install dependencies into a shared directory prefix.

1. Add a build recipe X in [`ci/build_container/build_recipes`](../ci/build_container/build_recipes)
   for developer-local and CI external dependency build flows.
2. Add a build target Y in [`ci/prebuilt/BUILD`](../ci/prebuilt/BUILD) to consume the headers and
   libraries produced by the build recipe X.
3. Add a map from target Y to build recipe X in [`target_recipes.bzl`](target_recipes.bzl).
4. Reference your new external dependency in some `envoy_cc_library` via Y in the `external_deps`
   attribute.
5. `bazel test //test/...`

# Updating an external dependency version

1. Update the build recipe in [`ci/build_container/build_recipes`](../ci/build_container/build_recipes).
2. `bazel test //test/...`

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
to binaries, libraries, headers, etc.

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

1. If the dependency is a build recipe, update the build recipe in
[`ci/build_container/build_recipes`](../ci/build_container/build_recipes).
2. If not, update the corresponding entry in
[the repository locations file.](https://github.com/envoyproxy/envoy/blob/master/bazel/repository_locations.bzl)
3. `bazel test //test/...`

# Overriding an external dependency temporarily

An external dependency built by genrule repository or native Bazel could be overridden by
specifying Bazel option
[`--override_repository`](https://docs.bazel.build/versions/master/command-line-reference.html)
to point to a local copy. The option can used multiple times to override multiple dependencies.
The name of the dependency can be found in
[the repository locations file.](https://github.com/envoyproxy/envoy/blob/master/bazel/repository_locations.bzl)
The path of the local copy has to be absolute path.

# Distdir - prefetching dependencies

Usually Bazel downloads all dependencies during build time. But there is a
possibility to prefetch dependencies and point Bazel to them by using `--distdir`
option and providing a path to directory which contains tarballs with exactly
the same name and the same SHA256 sum that are defined in repositories
definitions.

For example, let's assume that your distdir location is `$HOME/envoy_distdir`.
To prefetch `boringssl` which is defined in `bazel/repository_locations.bzl` as:

```
boringssl = dict(
    # Use commits from branch "chromium-stable-with-bazel"
    sha256 = "d1700e0455f5f918f8a85ff3ce6cd684d05c766200ba6bdb18c77d5dcadc05a1",
    strip_prefix = "boringssl-060e9a583976e73d1ea8b2bfe8b9cab33c62fa17",
    # chromium-70.0.3538.67
    urls = ["https://github.com/google/boringssl/archive/060e9a583976e73d1ea8b2bfe8b9cab33c62fa17.tar.gz"],
),
```

`$HOME/envoy_distdir` needs to contain `060e9a583976e73d1ea8b2bfe8b9cab33c62fa17.tar.gz`
file.

Then Envoy needs to be built with the following command:

```
bazel build --distdir=$HOME/envoy_distdir //source/exe:envoy
```

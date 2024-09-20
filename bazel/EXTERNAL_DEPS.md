# Choosing tarballs

Where the dependency maintainer provides a tarball, prefer that over the
automatically generated Github tarball. Github generated tarball SHA256
values can change when Github change their tar/gzip libraries breaking
builds. Maintainer provided tarballs are more stable and the maintainer
can provide the SHA256.

# Adding external dependencies to Envoy (C++)

## Native Bazel

This is the preferred style of adding dependencies that use Bazel for their
build process.

1. Define a new Bazel repository in [`bazel/repositories.bzl`](repositories.bzl),
   in the `envoy_dependencies()` function.
2. Reference your new external dependency in some `envoy_cc_library` via the
   `deps` attribute.
3. `bazel test //test/...`

## External CMake (preferred)

This is the preferred style of adding dependencies that use CMake for their build system.

1. Define a the source Bazel repository in [`bazel/repositories.bzl`](repositories.bzl), in the
   `envoy_dependencies()` function.
2. Add an `envoy_cmake` rule to [`bazel/foreign_cc/BUILD`](foreign_cc/BUILD). This will reference
   the source repository in step 1.
3. Reference your new external dependency in some `envoy_cc_library` via the name bound in step 1
   `deps` attribute.
4. `bazel test //test/...`

# Adding external dependencies to Envoy (Python)

Python dependencies should be added via `pip` and `rules_python`. The process
is:

1. Define a `pip_install()` pointing at your target `requirements.txt` in
   [`bazel/repositories_extra.bzl`](repositories_extra.bzl)

2. Add a `requirements("<package name>")` in the `BUILD` file that depends on
   this package.

You can use [`tools/config_validation/BUILD`](../tools/config_validation/BUILD) as an example
for this flow. See also the [`rules_python`](https://github.com/bazelbuild/rules_python)
documentation for further references.

# Updating an external dependency version

1. Update the corresponding entry in
[the repository locations file.](https://github.com/envoyproxy/envoy/blob/main/bazel/repository_locations.bzl)
2. `bazel test //test/...`

# Overriding an external dependency temporarily

An external dependency built by genrule repository or native Bazel could be overridden by
specifying Bazel option
[`--override_repository`](https://docs.bazel.build/versions/master/command-line-reference.html)
to point to a local copy. The option can used multiple times to override multiple dependencies.
The name of the dependency can be found in
[the repository locations file.](https://github.com/envoyproxy/envoy/blob/main/bazel/repository_locations.bzl)
The path of the local copy has to be absolute path.

For repositories built by `envoy_cmake()` in `bazel/foreign_cc/BUILD`,
it is necessary to populate the local copy with some additional Bazel machinery
to support `--override_repository`:
1. Place an empty `WORKSPACE` in the root.
2. Place a `BUILD` file with `filegroup(name = "all", srcs = glob(["**"]), visibility = ["//visibility:public"])`
   in the root.

# Debugging external dependencies

For all external dependencies, overriding with a local copy as described in the
previous section is a useful tool.

Below we describe specific tips for obtaining additional debug for specific
dependencies:

* `libevent`: add `"EVENT__ENABLE_VERBOSE_DEBUG": "on",` to `cache_entries`
  in the `event` target in `bazel/foreign_cc/BUILD` for verbose tracing of
  libevent processing.

* `nghttp2`: set `ENVOY_NGHTTP2_TRACE` in the environment and run at `-l trace`.

* `QUICHE`: set `ENVOY_QUICHE_VERBOSITY=n` in the environment to display
  verbose logs up to level `n`.

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

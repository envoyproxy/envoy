# Contributing guide

## API changes

All API changes should follow the [style guide](STYLE.md).

The following high level procedure is used to make Envoy changes that require API changes.

1. Create a PR in this repo for the API/configuration changes. (If it helps to discuss the
   configuration changes in the context of a code change, it is acceptable to point a code
   change at a temporary fork of this repo so it passes tests).

   Run the automated formatting checks on your change before submitting the PR:

   ```
   ./ci/run_envoy_docker.sh './ci/do_ci.sh check_format'
   ```

   If the `check_format` script reports any problems, you can fix them manually or run
   the companion `fix_format` script:

   ```
   ./ci/run_envoy_docker.sh './ci/do_ci.sh fix_format'
   ```

   Before building the docs


2. Bazel can be used to build/test locally.
   1. Directly on Linux:
      ```
      bazel build //envoy/...
      bazel test //test/... //tools/...
      ```
   2. Using docker:
      ```
      ./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.test'
      ./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.docs'
      ```
      *Note: New .proto files should be also included to [build.sh](https://github.com/envoyproxy/data-plane-api/blob/4e533f22baced334c4aba68fb60c5fc439f0fe9c/docs/build.sh#L28) and
      [BUILD](https://github.com/envoyproxy/data-plane-api/blob/master/docs/BUILD) in order to get the RSTs generated.*

3. All configuration changes should have temporary associated documentation. Fields should be
   hidden from the documentation via the `[#not-implemented-hide:]` comment tag. E.g.,

   ```
   // [#not-implemented-hide:] Some new cool field that I'm going to implement and then
   // come back and doc for real!
   string foo_field = 3;
   ```

   Additionally, [constraints](https://github.com/lyft/protoc-gen-validate/blob/master/README.md)
   should be specified for new fields if applicable. E.g.,

   ```
   string endpoint = 2 [(validate.rules).message.required = true];
   ```

4. Next, the feature should be implemented in Envoy. New versions of data-plane-api are brought
   in via editing [this](https://github.com/envoyproxy/envoy/blob/master/bazel/repository_locations.bzl)
   file.
5. Once (4) is completed, come back here and unhide the field from documentation and complete all
   documentation around the new feature. This may include architecture docs, etc. Optimally, the
   PR for documentation should be reviewed at the same time that the feature PR is reviewed in
   the Envoy repository. See the following section for tips on writing documentation.

## Documentation changes

The Envoy project takes documentation seriously. We view it as one of the reasons the project has
seen rapid adoption. As such, it is required that all features have complete documentation. This is
generally going to be a combination of API documentation as well as architecture/overview
documentation.

### Building documentation locally

The documentation can be built locally in the root of this repo via:

```
docs/build.sh
```

Or to use a hermetic docker container:

```
./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.docs'
```

This process builds RST documentation directly from the proto files, merges it with the static RST
files, and then runs [Sphinx](http://www.sphinx-doc.org/en/stable/rest.html) over the entire tree to
produce the final documentation. The generated RST files are not committed as they are regenerated
every time the documentation is built.

### Viewing documentation

Once the documentation is built, it is available rooted at `generated/docs/index.html`. The
generated RST files are also viewable in `generated/rst`.

Note also that the generated documentation can be viewed in CI:

1. Open docs job in CircleCI.
2. Navigate to "artifacts" tab.
3. Expand files and click on `index.html`.

If you do not see an artifacts tab this is a bug in CircleCI. Try logging out and logging back in.

### Documentation guidelines

The following are some general guidelines around documentation.

* Cross link as much as possible. Sphinx is fantastic at this. Use it! See ample examples with the
  existing documentation as a guide.
* Please use a **single space** after a period in documentation so that all generated text is
  consistent.
* Comments can be left inside comments if needed (that's pretty deep, right?) via the `[#comment:]`
  special tag. E.g.,

  ```
  // This is a really cool field!
  // [#comment:TODO(mattklein123): Do something cooler]
  string foo_field = 3;
  ```

* Prefer *italics* for emphasis as `backtick` emphasis is somewhat jarring in our Sphinx theme.
* All documentation is expected to use proper English grammar with proper punctuation. If you are
  not a fluent English speaker please let us know and we will help out.
* Tag messages/enum/files with `[#proto-status: draft|experimental|frozen]` to
  reflect their [API
  status](https://www.envoyproxy.io/docs/envoy/latest/configuration/overview/v2_overview#status).
  Frozen entities do not need to be tagged except when overriding an outer scope
  draft or experimental status.

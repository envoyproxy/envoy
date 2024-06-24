# Building documentation locally

There are two methods to build the documentation, described below.

In both cases, the generated output can be found in `generated/docs`.

## Building in an existing Envoy development environment

If you have an [existing Envoy development environment](https://github.com/envoyproxy/envoy/tree/main/bazel#quick-start-bazel-build-for-developers), you should have the necessary dependencies and requirements and be able to build the documentation directly.

If using the Docker build container, you can run:

```bash
./ci/do_ci.sh docs
```

By default configuration examples are going to be validated during build. To disable validation,
set `SPHINX_SKIP_CONFIG_VALIDATION` environment variable to `true`:

```bash
SPHINX_SKIP_CONFIG_VALIDATION=true ./ci/do_ci.sh docs
```

If not using the Docker build container, you can run:

```bash
bazel run --//tools/tarball:target=//docs:html //tools/tarball:unpack "$PWD"/generated/docs/
```

## Using the Docker build container to build the documentation

If you *do not* have an existing development environment, you may wish to use the Docker build
image that is used in continuous integration.

This can be done as follows:

```
./ci/run_envoy_docker.sh './ci/do_ci.sh docs'
```

To use this method you will need a minimum of 4-5GB of disk space available to accommodate the build image.

# Creating a Pull Request with documentation changes

When you create a Pull Request the documentation is rendered by Azure Pipelines.

To do this:
1. Open docs job in Azure Pipelines.
2. Navigate to "Upload Docs to GCS" log.
3. Click on the link there.

# How the Envoy website and docs are updated


The docs are published dynamically by Netlify on every commit to main. This process is handled by the
[envoy-website repo](https://github.com/envoyproxy/envoy-website)

For tagged commits the docs are built statically by the [archive repo](https://github.com/envoyproxy/archive),
which in turn triggers a rebuild of the website.

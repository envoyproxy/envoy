# Building documentation locally

There are two methods to build the documentation, described below.

In both cases, the generated output can be found in `generated/docs`.

## Building in an existing Envoy development environment

If you have an [existing Envoy development environment](https://github.com/envoyproxy/envoy/tree/master/bazel#quick-start-bazel-build-for-developers), you should have the necessary dependencies and requirements and be able to build the documentation directly.

```bash
./docs/build.sh
```

By default configuration examples are going to be validated during build. To disable validation,
set `SPHINX_SKIP_CONFIG_VALIDATION` environment variable to `true`:

```bash
SPHINX_SKIP_CONFIG_VALIDATION=true docs/build.sh
```

## Using the Docker build container to build the documentation

If you *do not* have an existing development environment, you may wish to use the Docker build
image that is used in continuous integration.

This can be done as follows:

```
./ci/run_envoy_docker.sh 'docs/build.sh'
```

To use this method you will need a minimum of 4-5GB of disk space available to accommodate the build image.

# Creating a Pull Request with documentation changes

When you create a Pull Request the documentation is rendered by Azure Pipelines.

To do this:
1. Open docs job in Azure Pipelines.
2. Navigate to "Upload Docs to GCS" log.
3. Click on the link there.

# How the Envoy website and docs are updated

1. The docs are published to [docs/envoy/latest](https://github.com/envoyproxy/envoyproxy.github.io/tree/master/docs/envoy/latest)
   on every commit to master. This process is handled by Azure Pipelines with the
  [`publish.sh`](https://github.com/envoyproxy/envoy/blob/master/docs/publish.sh) script.

2. The docs are published to [docs/envoy](https://github.com/envoyproxy/envoyproxy.github.io/tree/master/docs/envoy)
   in a directory named after every tagged commit in this repo. Thus, on every tagged release there
   are snapped docs.

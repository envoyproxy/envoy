# Envoy Docker Distribution

This directory contains the Docker build configuration for Envoy container images.

## Files

- `Dockerfile-envoy` - Main Dockerfile for building Envoy container images
- `buildd.sh` - Script for building Docker images in CI
- `docker-entrypoint.sh` - Entrypoint script for Envoy containers

## Usage

The Docker build is typically invoked through the main CI script:

```bash
./ci/do_ci.sh docker
```

This will build Docker images for multiple platforms and variants including:
- Standard Envoy image
- Debug image
- Contrib image (with additional extensions)
- Distroless image
- Google VRP image
- Tools image

## Development

For local development, you can build images directly using the build.sh script:

```bash
DOCKER_CI_DRYRUN=1 ./distribution/docker/build.sh
```

This will show what commands would be executed without actually building images.

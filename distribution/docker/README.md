# Envoy container images

The Envoy container images are built with Bazel (rules_oci), from the release binaries.

## Files

- `BUILD` - The images: `envoy`, `envoy-debug`, `envoy-contrib`, `envoy-contrib-debug`,
  `envoy-distroless`, `envoy-contrib-distroless` and `envoy-tools`
- `ca_certificates.bzl` - The CA certificates configuration of the Ubuntu based images
- `docker-entrypoint.sh` - Entrypoint script for the Ubuntu based images

The base images are pinned by digest in `MODULE.bazel` (`oci.pull`), and updated by the
`release-images` task of the dependency workflow (`ci/dependency/release_images.sh`).

## Usage

Build the images, each as an OCI tarball, for the host's architecture:

```bash
./ci/do_ci.sh release.containers
```

The release build (`./ci/do_ci.sh release.publish`, or `release.multiarch` for both architectures)
builds them too, with the packages.

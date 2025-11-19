# Fix for Buildah MediaType Issue - Toolshed Action

## Summary for Toolshed Maintainers

The Envoy project's multi-arch container images (v1.35.1+) are missing the `mediaType` field in their OCI image index manifests, causing failures with tools like rules_oci in Bazel.

## Root Cause

The `envoyproxy/toolshed/gh-actions/oci/collector` action uses buildah to create and push multi-arch manifests. Buildah has a known bug where it omits the `mediaType` field when pushing OCI format manifests.

## Required Fix in Toolshed Repository

**Repository**: `envoyproxy/toolshed`
**File**: `gh-actions/oci/collector/action.yml` (or wherever buildah manifest push is called)

**Change Required**:

Add the `--format=v2s2` flag to the `buildah manifest push` command:

```bash
# Before (broken):
buildah manifest push --all <manifest-name> docker://<registry>/<repo>:<tag>

# After (fixed):
buildah manifest push --all --format=v2s2 <manifest-name> docker://<registry>/<repo>:<tag>
```

## Why This Works

- The `--format=v2s2` flag tells buildah to use Docker manifest list format
- Docker format always includes the `mediaType` field
- The resulting manifest is still multi-arch and compatible with all tools
- The mediaType will be `application/vnd.docker.distribution.manifest.list.v2+json` (Docker format) instead of missing entirely

## Alternative Approaches (Not Recommended)

1. **Wait for buildah fix**: Issues #4395 and #5051 are open but no timeline for resolution
2. **Post-process manifests**: Complex and fragile
3. **Switch to different tooling**: Unnecessary when a simple flag fixes it

## Testing the Fix

After implementing, verify by inspecting a pushed manifest:

```bash
skopeo inspect --raw docker://docker.io/envoyproxy/envoy:distroless-v1.35.7 | jq .mediaType
# Should return: "application/vnd.docker.distribution.manifest.list.v2+json"
```

And test with rules_oci:
```starlark
oci.pull(
    name = "envoy",
    image = "index.docker.io/envoyproxy/envoy",
    platforms = ["linux/amd64", "linux/arm64"],
    tag = "distroless-v1.35.7",
)
# Should work without "key 'mediaType' not found" error
```

## Impact

- **Severity**: High - Blocks Bazel users from using Envoy images
- **Scope**: All Envoy container images from v1.35.1 onwards
- **Fix Complexity**: Trivial (one flag addition)
- **Fix Location**: Toolshed repository, not Envoy repository

## References

- Envoy issue: https://github.com/envoyproxy/envoy/issues/41864
- Buildah issue: https://github.com/containers/buildah/issues/4395
- OCI spec: https://github.com/opencontainers/image-spec/blob/main/image-index.md
- Research document: See BUILDAH_MANIFEST_MEDIATYPE_ISSUE.md in this directory

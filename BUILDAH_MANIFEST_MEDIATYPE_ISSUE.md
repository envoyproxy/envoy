# Buildah Multi-Arch Manifest MediaType Issue - Research Findings

## Issue Summary

Starting with Envoy v1.35.1, multi-arch container images pushed to Docker Hub are missing the `mediaType` field at the top level of the OCI image index manifest. This causes errors when using tools like rules_oci in Bazel:

```
ERROR: key "mediaType" not found in dictionary
```

## Root Cause Analysis

### What Changed in v1.35.1

Commit `01cfa86465` ("docker/ci: Build oci images natively #40557") introduced a new workflow that uses buildah via the `envoyproxy/toolshed/gh-actions/oci/collector` action to create and push multi-arch manifests.

Previously working v1.35.0 manifest structure:
- Used a different build process that properly set the mediaType field

Current v1.35.1+ broken manifest structure:
```json
{
    "schemaVersion": 2,
    "manifests": [
        {
            "mediaType": "application/vnd.oci.image.manifest.v1+json",
            "digest": "sha256:...",
            "size": 3317,
            "platform": {
                "architecture": "amd64",
                "os": "linux"
            }
        },
        ...
    ]
}
```

Expected manifest structure per OCI spec:
```json
{
    "schemaVersion": 2,
    "mediaType": "application/vnd.oci.image.index.v1+json",
    "manifests": [
        {
            "mediaType": "application/vnd.oci.image.manifest.v1+json",
            "digest": "sha256:...",
            "size": 3317,
            "platform": {
                "architecture": "amd64",
                "os": "linux"
            }
        },
        ...
    ]
}
```

## OCI Specification Analysis

According to the [OCI Image Index Specification](https://github.com/opencontainers/image-spec/blob/main/image-index.md):

- The `mediaType` field is **SHOULD** be present (strongly recommended but not strictly required)
- When present, it MUST be set to `"application/vnd.oci.image.index.v1+json"`
- The field aids in forward/backward compatibility and helps tools identify the manifest type

## Buildah Known Issues

This is a **known bug in buildah**:

### Issue #4395: MediaType not set when pushing OCI Image Index
- **Link**: https://github.com/containers/buildah/issues/4395
- **Status**: Open
- **Description**: When buildah pushes an OCI Image Index manifest using the OCI format, it omits the `mediaType` field at the top level
- **Impact**: Tools like rules_oci, Google Jib, and others that strictly validate manifests will fail

### Issue #5051: Image index media type and annotation
- **Link**: https://github.com/containers/buildah/issues/5051
- **Status**: Open
- **Description**: Related issues with mediaType handling in manifest lists
- **Impact**: Compatibility issues between Docker and OCI formats

## Who's at Fault?

1. **Buildah**: Has a bug where it doesn't set the mediaType field when using OCI format
2. **OCI Spec**: Says "SHOULD" instead of "MUST", making it technically optional
3. **rules_oci**: Could be more lenient and handle missing mediaType, but it's reasonable to expect compliance with "SHOULD" directives

The consensus is that **buildah should be fixed** to include the mediaType field for better compatibility.

## Recommended Solutions

### Option 1: Use Docker Manifest Format (Immediate Fix)

In the `envoyproxy/toolshed/gh-actions/oci/collector` action, modify the `buildah manifest push` command to use Docker format:

```bash
buildah manifest push --all --format=v2s2 <manifest-list> docker://<registry>/<image>:<tag>
```

**Pros**:
- Immediate fix that works now
- Docker manifest format always includes the mediaType field
- Still multi-arch compatible
- Widely supported by all tools

**Cons**:
- Uses Docker format instead of OCI format
- mediaType will be `application/vnd.docker.distribution.manifest.list.v2+json` instead of `application/vnd.oci.image.index.v1+json`

### Option 2: Wait for Buildah Fix (Long-term)

Monitor buildah issues #4395 and #5051 for fixes and upgrade when available.

**Pros**:
- Uses true OCI format
- Proper long-term solution

**Cons**:
- No timeline for fix
- Envoy users blocked in the meantime

### Option 3: Post-Process Manifests

After pushing, fetch and modify the manifest to add the mediaType field, then re-push.

**Pros**:
- Uses OCI format

**Cons**:
- Complex implementation
- Requires registry API access
- Fragile and error-prone

## Recommended Action

**For Envoy maintainers:**

The fix needs to be made in the **toolshed repository** in the `gh-actions/oci/collector/action.yml` file. The specific change would be to add `--format=v2s2` flag to the buildah manifest push command.

**File to fix**: `envoyproxy/toolshed/gh-actions/oci/collector/action.yml`

**Command change**:
```bash
# Current (broken):
buildah manifest push --all <manifest> docker://...

# Fixed (working):
buildah manifest push --all --format=v2s2 <manifest> docker://...
```

This is a **one-line fix** that will restore compatibility with rules_oci and other tools.

## Verification

After the fix is implemented, verify by:

1. Inspecting the manifest from Docker Hub:
   ```bash
   skopeo inspect --raw docker://docker.io/envoyproxy/envoy:distroless-vX.Y.Z | jq .mediaType
   ```
   Should return: `"application/vnd.docker.distribution.manifest.list.v2+json"`

2. Testing with rules_oci in Bazel:
   ```starlark
   oci.pull(
       name = "envoy",
       digest = "sha256:...",
       image = "index.docker.io/envoyproxy/envoy",
       platforms = ["linux/amd64", "linux/arm64"],
       tag = "distroless-vX.Y.Z",
   )
   ```

## References

- Envoy Issue: https://github.com/envoyproxy/envoy/issues/41864
- OCI Image Index Spec: https://github.com/opencontainers/image-spec/blob/main/image-index.md
- Buildah Issue #4395: https://github.com/containers/buildah/issues/4395
- Buildah Issue #5051: https://github.com/containers/buildah/issues/5051
- Commit that introduced the issue: 01cfa864651a6d4ce0de0b03bd6ab202599eb6dd

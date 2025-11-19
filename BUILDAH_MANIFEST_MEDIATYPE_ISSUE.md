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

## Buildah Known Issues and Version Problem

This is a **known bug in older versions of buildah**:

### Buildah Issue 4395: MediaType not set when pushing OCI Image Index
- **Link**: https://github.com/containers/buildah/issues/4395
- **Status**: Closed/Fixed in buildah 1.32+ (July 2023)
- **Description**: When buildah pushes an OCI Image Index manifest using the OCI format, it omits the `mediaType` field at the top level
- **Impact**: Tools like rules_oci, Google Jib, and others that strictly validate manifests will fail

### Buildah Issue 5051: Image index media type and annotation
- **Link**: https://github.com/containers/buildah/issues/5051
- **Status**: Closed/Fixed in buildah 1.31+ via PR 5301
- **Description**: Buildah couldn't create OCI format image indexes with proper mediaType and annotations
- **Impact**: Compatibility issues between Docker and OCI formats

### The Real Problem: Outdated Buildah Version

The Envoy workflow uses **ubuntu-22.04** GitHub Actions runners, which come with **buildah 1.23.1** (from 2022). This version predates both fixes:
- Buildah 1.23.1 is from 2022
- [Buildah issue 5051](https://github.com/containers/buildah/issues/5051) was fixed in buildah 1.31 (2023)
- [Buildah issue 4395](https://github.com/containers/buildah/issues/4395) was fixed in buildah 1.32 (2023)

**Therefore, the root cause is using an outdated version of buildah that has this bug.**

## Who's at Fault?

1. **GitHub Actions ubuntu-22.04 runners**: Ship with outdated buildah 1.23.1 (from 2022)
2. **Buildah 1.23.1**: Has the bug where it doesn't set the mediaType field when using OCI format
3. **OCI Spec**: Says "SHOULD" instead of "MUST", making it technically optional
4. **rules_oci**: Could be more lenient and handle missing mediaType, but it's reasonable to expect compliance with "SHOULD" directives

The consensus is that **buildah should be upgraded** to 1.32+ for better compatibility.

## Recommended Solutions

### Option 1: Upgrade Buildah (Best Long-term Solution)

Upgrade the buildah version used in the GitHub Actions workflow to 1.32 or later, which has the fix.

In the `envoyproxy/toolshed/gh-actions/oci/collector` action, add a step to install a newer buildah:

```yaml
- name: Install newer buildah
  run: |
    # Install buildah 1.32+ from kubic repository or build from source
    sudo add-apt-repository -y ppa:projectatomic/ppa
    sudo apt-get update
    sudo apt-get install -y buildah
```

Or switch to ubuntu-24.04 runners which have buildah 1.33.

**Pros**:
- Proper fix that addresses the root cause
- Uses true OCI format with mediaType
- Future-proof solution

**Cons**:
- Requires modifying the runner setup
- Needs testing to ensure compatibility

### Option 2: Use Docker Manifest Format (Immediate Workaround)

In the `envoyproxy/toolshed/gh-actions/oci/collector` action, modify the `buildah manifest push` command to use Docker format:

```bash
buildah manifest push --all --format=v2s2 <manifest-list> docker://<registry>/<image>:<tag>
```

**Pros**:
- Immediate fix that works with buildah 1.23.1
- Docker manifest format always includes the mediaType field
- Still multi-arch compatible
- Widely supported by all tools

**Cons**:
- Uses Docker format instead of OCI format
- mediaType will be `application/vnd.docker.distribution.manifest.list.v2+json` instead of `application/vnd.oci.image.index.v1+json`
- Doesn't address the root cause (old buildah)

### Option 3: Wait for Ubuntu 24.04 Runners

GitHub Actions ubuntu-24.04 runners come with buildah 1.33, which has the fix.

**Pros**:
- Uses OCI format with proper mediaType
- No custom buildah installation needed

**Cons**:
- ubuntu-24.04 runners may not be stable yet
- Requires workflow changes

### Option 4: Post-Process Manifests

After pushing, fetch and modify the manifest to add the mediaType field, then re-push.

**Pros**:
- Uses OCI format

**Cons**:
- Complex implementation
- Requires registry API access
- Fragile and error-prone

## Recommended Action

**For Envoy maintainers:**

The fix needs to be made in the **toolshed repository** in the `gh-actions/oci/collector/action.yml` file. You have three options:

**Best solution (long-term)**: Upgrade to buildah 1.32+ by either:
- Installing a newer buildah version in the action
- Switching to ubuntu-24.04 runners (buildah 1.33)

**Quick workaround (immediate)**: Add `--format=v2s2` flag to the buildah manifest push command.

**Specific changes needed**:

For the workaround:
```bash
# Current (broken with buildah 1.23.1):
buildah manifest push --all <manifest> docker://...

# Fixed (working with buildah 1.23.1):
buildah manifest push --all --format=v2s2 <manifest> docker://...
```

For the proper fix:
```yaml
# Add before using buildah:
- name: Install buildah 1.32+
  run: |
    # Install from kubic or other repository with newer buildah
    sudo apt-get install -y buildah>=1.32
```

**Version Requirements**:
- Current: buildah 1.23.1 (ubuntu-22.04) - **HAS THE BUG**
- Fixed: buildah 1.32+ - **BUG IS FIXED**

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
- Buildah Issue 4395: https://github.com/containers/buildah/issues/4395
- Buildah Issue 5051: https://github.com/containers/buildah/issues/5051
- Commit that introduced the issue: 01cfa864651a6d4ce0de0b03bd6ab202599eb6dd

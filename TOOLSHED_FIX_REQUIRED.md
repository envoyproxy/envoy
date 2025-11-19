# Fix for Buildah MediaType Issue - Toolshed Action

## Summary for Toolshed Maintainers

The Envoy project's multi-arch container images (v1.35.1+) are missing the `mediaType` field in their OCI image index manifests, causing failures with tools like rules_oci in Bazel.

## Root Cause

The `envoyproxy/toolshed/gh-actions/oci/collector` action uses buildah to create and push multi-arch manifests. The workflow runs on **ubuntu-22.04** which has **buildah 1.23.1** (from 2022). This version has a known bug where it omits the `mediaType` field when pushing OCI format manifests.

**The bugs were fixed in buildah 1.31-1.32 (2023), but ubuntu-22.04 still ships with the old 1.23.1 version.**

## Required Fix in Toolshed Repository

**Repository**: `envoyproxy/toolshed`
**File**: `gh-actions/oci/collector/action.yml` (or wherever buildah is used)

### Option 1: Upgrade Buildah (Recommended)

Install buildah 1.32+ before using it:

```yaml
- name: Install buildah 1.32+
  run: |
    # Method 1: From kubic repository
    echo 'deb http://download.opensuse.org/repositories/devel:/kubic:/libcontainers:/stable/xUbuntu_22.04/ /' | sudo tee /etc/apt/sources.list.d/devel:kubic:libcontainers:stable.list
    curl -fsSL https://download.opensuse.org/repositories/devel:kubic:libcontainers:stable/xUbuntu_22.04/Release.key | gpg --dearmor | sudo tee /etc/apt/trusted.gpg.d/devel_kubic_libcontainers_stable.gpg > /dev/null
    sudo apt-get update
    sudo apt-get install -y buildah
    buildah --version  # Should show 1.32+
```

Or switch to ubuntu-24.04 runners (has buildah 1.33).

### Option 2: Use Docker Format (Workaround)

If upgrading is not immediately possible, use the Docker format flag:

```bash
# Before (broken with buildah 1.23.1):
buildah manifest push --all <manifest-name> docker://<registry>/<repo>:<tag>

# After (workaround for buildah 1.23.1):
buildah manifest push --all --format=v2s2 <manifest-name> docker://<registry>/<repo>:<tag>
```

## Why This Works

### Option 1: Upgrade Buildah
- Buildah 1.32+ has the bug fix that properly sets the `mediaType` field
- Results in proper OCI format manifests with `application/vnd.oci.image.index.v1+json`
- This is the proper long-term solution

### Option 2: Docker Format Workaround
- The `--format=v2s2` flag tells buildah to use Docker manifest list format
- Docker format always includes the `mediaType` field even in buildah 1.23.1
- The resulting manifest is still multi-arch and compatible with all tools
- The mediaType will be `application/vnd.docker.distribution.manifest.list.v2+json` (Docker format)

## Alternative Approaches (Not Recommended)

1. **Wait for ubuntu-24.04 to become default**: Has buildah 1.33 but may not be stable
2. **Post-process manifests**: Complex and fragile
3. **Switch to different tooling**: Unnecessary when buildah upgrade or flag fixes it

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
- **Root Cause**: Outdated buildah 1.23.1 on ubuntu-22.04 runners
- **Fix Complexity**: 
  - Upgrade buildah: Moderate (add installation step)
  - Workaround flag: Trivial (one flag addition)
- **Fix Location**: Toolshed repository, not Envoy repository

## References

- Envoy issue: https://github.com/envoyproxy/envoy/issues/41864
- Buildah issue: https://github.com/containers/buildah/issues/4395
- OCI spec: https://github.com/opencontainers/image-spec/blob/main/image-index.md
- Research document: See BUILDAH_MANIFEST_MEDIATYPE_ISSUE.md in this directory

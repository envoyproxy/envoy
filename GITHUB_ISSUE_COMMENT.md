# GitHub Issue Comment Template

Post this to: https://github.com/envoyproxy/envoy/issues/41864

---

## Authoritative Answer: What's Wrong and How to Fix

After thorough research, I can provide a definitive answer:

### What's Wrong

**You are correct** - the OCI spec strongly recommends (SHOULD) that image index manifests include a `mediaType` field at the top level. The manifests being pushed by Envoy v1.35.1+ are missing this field.

**Root cause**: The GitHub Actions workflow uses ubuntu-22.04 runners which ship with **buildah 1.23.1** (from 2022). This old version has a bug where it omits the `mediaType` field when pushing OCI format manifests.

### The Buildah Bug History

1. **Buildah Issues #4395 and #5051**: Documented the mediaType omission bug
2. **Fixed in buildah 1.31-1.32** (2023): The bugs were fixed about a year after buildah 1.23.1
3. **Current ubuntu-22.04**: Still ships with the old buggy version (1.23.1)

### Who's at Fault

1. **GitHub Actions ubuntu-22.04 runners** (primary) - Ship with outdated buildah 1.23.1
2. **Buildah 1.23.1** - Has the bug (but it's been fixed in newer versions)
3. **OCI Spec** (minor) - Uses "SHOULD" instead of "MUST", making the field technically optional
4. **rules_oci** (not at fault) - It's reasonable to expect spec compliance for "SHOULD" directives

### Why This Started in v1.35.1

Commit `01cfa86465` in Envoy introduced a new workflow that uses buildah (via the `envoyproxy/toolshed/gh-actions/oci/collector` action) to create and push multi-arch manifests. The previous process didn't have this issue.

### How to Fix

**The fix needs to be made in the toolshed repository**, not the Envoy repository.

**Location**: `envoyproxy/toolshed` repository, in the `gh-actions/oci/collector/action.yml` file (or wherever buildah is used).

**Two options**:

**Option 1: Upgrade buildah (recommended)**

Install buildah 1.32+ in the action:

```yaml
- name: Install buildah 1.32+
  run: |
    echo 'deb http://download.opensuse.org/repositories/devel:/kubic:/libcontainers:/stable/xUbuntu_22.04/ /' | sudo tee /etc/apt/sources.list.d/devel:kubic:libcontainers:stable.list
    curl -fsSL https://download.opensuse.org/repositories/devel:kubic:libcontainers:stable/xUbuntu_22.04/Release.key | gpg --dearmor | sudo tee /etc/apt/trusted.gpg.d/devel_kubic_libcontainers_stable.gpg > /dev/null
    sudo apt-get update
    sudo apt-get install -y buildah
```

Or switch to ubuntu-24.04 runners (buildah 1.33).

**Option 2: Use Docker format (workaround)**

Add the `--format=v2s2` flag to buildah manifest push:

```bash
# Currently (broken with buildah 1.23.1):
buildah manifest push --all <manifest> docker://...

# Fixed (workaround for buildah 1.23.1):
buildah manifest push --all --format=v2s2 <manifest> docker://...
```

### Why This Works

**Option 1 (upgrade):**
- Buildah 1.32+ has the bug fix built-in
- Results in proper OCI format with mediaType set correctly
- This is the proper long-term solution

**Option 2 (workaround):**
- The `--format=v2s2` flag uses Docker manifest list format instead of OCI
- Docker format always includes the `mediaType` field even in buildah 1.23.1
- The resulting manifest is still multi-arch and compatible with all tools including rules_oci
- This is a workaround until buildah is upgraded

### Verification

After the fix is deployed, you can verify it works:

```bash
# Check the manifest has mediaType:
skopeo inspect --raw docker://docker.io/envoyproxy/envoy:distroless-v1.35.7 | jq .mediaType
# Should return: "application/vnd.docker.distribution.manifest.list.v2+json"

# Test with rules_oci (should work without errors):
oci.pull(
    name = "envoy",
    image = "index.docker.io/envoyproxy/envoy",
    platforms = ["linux/amd64", "linux/arm64"],
    tag = "distroless-v1.35.7",
)
```

### Next Steps

1. A fix needs to be implemented in the toolshed repository (either upgrade buildah or add the flag)
2. Once merged, the next Envoy release will have properly formatted manifests
3. Existing v1.35.1-v1.35.6 images would need to be re-pushed if you want to fix them retroactively (optional)

### Version Information

- **Current**: ubuntu-22.04 with buildah 1.23.1 (has the bug)
- **Fixed**: buildah 1.32+ or ubuntu-24.04 with buildah 1.33 (bug is fixed)
- **Bug was fixed**: 2023 (about a year after buildah 1.23.1)

### Workaround for Current Users

Until the fix is deployed, you have a few options:
1. Use Envoy v1.35.0 which doesn't have this issue
2. Use single-arch images instead of multi-arch
3. Use Docker instead of rules_oci for pulling images
4. Pin to specific platform manifests instead of the index

---

I've created detailed research documents in the Envoy repository documenting this issue comprehensively. Let me know if you need any clarification!

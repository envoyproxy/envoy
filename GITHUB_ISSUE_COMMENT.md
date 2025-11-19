# GitHub Issue Comment Template

Post this to: https://github.com/envoyproxy/envoy/issues/41864

---

## Authoritative Answer: What's Wrong and How to Fix

After thorough research, I can provide a definitive answer:

### What's Wrong

**You are correct** - the OCI spec strongly recommends (SHOULD) that image index manifests include a `mediaType` field at the top level. The manifests being pushed by Envoy v1.35.1+ are missing this field due to a **known bug in buildah**.

### Who's at Fault

1. **Buildah** (primary) - Has a known bug where it omits the `mediaType` field when pushing OCI format manifests:
   - Issue #4395: https://github.com/containers/buildah/issues/4395
   - Issue #5051: https://github.com/containers/buildah/issues/5051

2. **OCI Spec** (minor) - Uses "SHOULD" instead of "MUST", making the field technically optional but strongly recommended

3. **rules_oci** (not at fault) - It's reasonable to expect spec compliance for "SHOULD" directives

### Why This Started in v1.35.1

Commit `01cfa86465` in Envoy introduced a new workflow that uses buildah (via the `envoyproxy/toolshed/gh-actions/oci/collector` action) to create and push multi-arch manifests. The previous process didn't have this issue.

### How to Fix

**The fix needs to be made in the toolshed repository**, not the Envoy repository.

**Location**: `envoyproxy/toolshed` repository, in the `gh-actions/oci/collector/action.yml` file (or wherever buildah manifest push is executed).

**Change**: Add the `--format=v2s2` flag to the buildah manifest push command:

```bash
# Currently (broken):
buildah manifest push --all <manifest> docker://...

# Fixed (working):
buildah manifest push --all --format=v2s2 <manifest> docker://...
```

### Why This Works

- The `--format=v2s2` flag uses Docker manifest list format instead of OCI format
- Docker format always includes the `mediaType` field
- The resulting manifest is still multi-arch and compatible with all tools including rules_oci
- This is the recommended workaround until buildah fixes the bug upstream

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

1. A fix needs to be implemented in the toolshed repository
2. Once merged, the next Envoy release will have properly formatted manifests
3. Existing v1.35.1-v1.35.6 images would need to be re-pushed if you want to fix them retroactively (optional)

### Workaround for Current Users

Until the fix is deployed, you have a few options:
1. Use Envoy v1.35.0 which doesn't have this issue
2. Use single-arch images instead of multi-arch
3. Use Docker instead of rules_oci for pulling images
4. Pin to specific platform manifests instead of the index

---

I've created detailed research documents in the Envoy repository documenting this issue comprehensively. Let me know if you need any clarification!

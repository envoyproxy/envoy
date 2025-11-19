# Summary: Buildah Multi-Arch Manifest MediaType Investigation

## Quick Answer

**What's wrong**: Buildah has a known bug where it doesn't set the `mediaType` field in OCI image index manifests.

**Where to fix**: The fix must be made in the **toolshed repository** (`envoyproxy/toolshed`), NOT in the Envoy repository.

**How to fix**: Add `--format=v2s2` flag to the buildah manifest push command in `gh-actions/oci/collector/action.yml`.

## Investigation Results

### Timeline
- **v1.35.0 and earlier**: Working correctly
- **v1.35.1 onwards**: Broken due to commit `01cfa86465` which switched to buildah for multi-arch manifest creation

### Technical Details
- **Buildah bug**: Known issues #4395 and #5051 on the containers/buildah repository
- **OCI spec**: Recommends (SHOULD) the mediaType field but doesn't require (MUST) it
- **Impact**: Tools like rules_oci in Bazel fail with "key 'mediaType' not found" error

### Solution
The `--format=v2s2` flag tells buildah to use Docker manifest list format, which always includes the mediaType field. While this uses Docker format instead of pure OCI format, it's the recommended workaround and is fully compatible with all tools.

## Documentation Files

1. **BUILDAH_MANIFEST_MEDIATYPE_ISSUE.md**
   - Comprehensive technical analysis
   - OCI spec interpretation
   - Buildah bug details
   - Full solution options

2. **TOOLSHED_FIX_REQUIRED.md**
   - Specific instructions for toolshed maintainers
   - Exact code changes needed
   - Testing procedures

3. **GITHUB_ISSUE_COMMENT.md**
   - Template for responding to issue #41864
   - User-friendly explanation
   - Workarounds for current users

## Next Steps

1. **For Envoy maintainers**: 
   - Open an issue or PR in the toolshed repository
   - Reference these documentation files
   - Coordinate the one-line fix

2. **For affected users**:
   - Use v1.35.0 as a temporary workaround
   - Or use single-arch images
   - Or use Docker instead of rules_oci
   - Or wait for the toolshed fix to be deployed

## Conclusion

This is a **buildah bug** with a **simple one-line fix** that needs to be applied in the **toolshed repository**. The Envoy repository itself doesn't need any code changes - only this documentation to guide the fix implementation.

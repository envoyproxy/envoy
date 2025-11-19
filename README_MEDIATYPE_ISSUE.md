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
- **Root cause**: GitHub Actions ubuntu-22.04 runners use buildah 1.23.1 (from 2022)
- **Buildah bug**: Issues [4395](https://github.com/containers/buildah/issues/4395) and [5051](https://github.com/containers/buildah/issues/5051) - fixed in buildah 1.31-1.32 (2023)
- **Current version**: 1.23.1 predates the fix by about a year
- **OCI spec**: Recommends (SHOULD) the mediaType field but doesn't require (MUST) it
- **Impact**: Tools like rules_oci in Bazel fail with "key 'mediaType' not found" error

### Solution
The `--format=v2s2` flag tells buildah to use Docker manifest list format, which always includes the mediaType field. This is a **workaround** for the outdated buildah 1.23.1.

**Better long-term solution**: Upgrade buildah to 1.32+ which has the bug fix, or switch to ubuntu-24.04 runners (buildah 1.33).

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
   - **Preferred**: Upgrade buildah to 1.32+ in the toolshed oci/collector action
   - **Alternative**: Add `--format=v2s2` flag as a workaround
   - **Option**: Switch to ubuntu-24.04 runners (buildah 1.33)
   - Reference these documentation files

2. **For affected users**:
   - Use v1.35.0 as a temporary workaround
   - Or use single-arch images
   - Or use Docker instead of rules_oci
   - Or wait for the toolshed fix to be deployed

## Conclusion

This is a **buildah version problem** - ubuntu-22.04 runners ship with buildah 1.23.1 which has the bug. The bug was fixed in buildah 1.32+. The solution is to either **upgrade buildah** (best) or use the **--format=v2s2 workaround** (quick fix). The Envoy repository itself doesn't need any code changes - only this documentation to guide the fix implementation in toolshed.

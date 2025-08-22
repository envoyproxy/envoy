load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# Detect bzlmod vs WORKSPACE context - in bzlmod, labels start with @@
_IS_BZLMOD = str(Label("//:invalid")).startswith("@@")

def envoy_http_archive(name, locations, location_name = None, **kwargs):
    # `existing_rule_keys` contains the names of repositories that have already
    # been defined in the Bazel workspace. By skipping repos with existing keys,
    # users can override dependency versions by using standard Bazel repository
    # rules in their WORKSPACE files.
    if name not in native.existing_rules():
        location = locations[location_name or name]

        # Context-aware repo_mapping handling.
        # The repo_mapping attribute is only supported in WORKSPACE builds with http_archive,
        # not in bzlmod module extensions. We detect the context using label inspection
        # and only filter repo_mapping in bzlmod builds.
        filtered_kwargs = {}
        for key, value in kwargs.items():
            # Only filter repo_mapping in bzlmod builds (not WORKSPACE)
            if _IS_BZLMOD and key == "repo_mapping":
                # Skip repo_mapping in bzlmod builds where it's not supported
                continue
            filtered_kwargs[key] = value

        # HTTP tarball at a given URL. Add a BUILD file if requested.
        http_archive(
            name = name,
            urls = location["urls"],
            sha256 = location["sha256"],
            strip_prefix = location.get("strip_prefix", ""),
            **filtered_kwargs
        )

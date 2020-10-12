load("@envoy_api//bazel:repository_locations_utils.bzl", "load_repository_locations_spec")

# Envoy dependencies may be annotated with the following attributes:
DEPENDENCY_ANNOTATIONS = [
    # List of the categories describing how the dependency is being used. This attribute is used
    # for automatic tracking of security posture of Envoy's dependencies.
    # Possible values are documented in the USE_CATEGORIES list below.
    # This attribute is mandatory for each dependecy.
    "use_category",

    # Attribute specifying CPE (Common Platform Enumeration, see https://nvd.nist.gov/products/cpe) ID
    # of the dependency. The ID may be in v2.3 or v2.2 format, although v2.3 is prefferred. See
    # https://nvd.nist.gov/products/cpe for CPE format. Use single wildcard '*' for version and vector elements
    # i.e. 'cpe:2.3:a:nghttp2:nghttp2:*'. Use "N/A" for dependencies without CPE assigned.
    # This attribute is optional for components with use categories listed in the
    # USE_CATEGORIES_WITH_CPE_OPTIONAL
    "cpe",
]

# NOTE: If a dependency use case is either dataplane or controlplane, the other uses are not needed
# to be declared.
USE_CATEGORIES = [
    # This dependency is used in API protos.
    "api",
    # This dependency is used in build process.
    "build",
    # This dependency is used to process xDS requests.
    "controlplane",
    # This dependency is used in processing downstream or upstream requests (core).
    "dataplane_core",
    # This dependency is used in processing downstream or upstream requests (extensions).
    "dataplane_ext",
    # This dependecy is used for logging, metrics or tracing (core). It may process unstrusted input.
    "observability_core",
    # This dependecy is used for logging, metrics or tracing (extensions). It may process unstrusted input.
    "observability_ext",
    # This dependency does not handle untrusted data and is used for various utility purposes.
    "other",
    # This dependency is used only in tests.
    "test_only",
]

# Components with these use categories are not required to specify the 'cpe'
# and 'last_updated' annotation.
USE_CATEGORIES_WITH_CPE_OPTIONAL = ["build", "other", "test_only", "api"]

def _fail_missing_attribute(attr, key):
    fail("The '%s' attribute must be defined for external dependecy " % attr + key)

# Method for verifying content of the repository location specifications.
#
# We also remove repository metadata attributes so that further consumers, e.g.
# http_archive, are not confused by them.
def load_repository_locations(repository_locations_spec):
    locations = {}
    for key, location in load_repository_locations_spec(repository_locations_spec).items():
        mutable_location = dict(location)
        locations[key] = mutable_location

        if "sha256" not in location or len(location["sha256"]) == 0:
            _fail_missing_attribute("sha256", key)

        if "project_name" not in location:
            _fail_missing_attribute("project_name", key)
        mutable_location.pop("project_name")

        if "project_desc" not in location:
            _fail_missing_attribute("project_desc", key)
        mutable_location.pop("project_desc")

        if "project_url" not in location:
            _fail_missing_attribute("project_url", key)
        project_url = mutable_location.pop("project_url")
        if not project_url.startswith("https://") and not project_url.startswith("http://"):
            fail("project_url must start with https:// or http://: " + project_url)

        if "version" not in location:
            _fail_missing_attribute("version", key)
        mutable_location.pop("version")

        if "use_category" not in location:
            _fail_missing_attribute("use_category", key)
        use_category = mutable_location.pop("use_category")

        if "dataplane_ext" in use_category or "observability_ext" in use_category:
            if "extensions" not in location:
                _fail_missing_attribute("extensions", key)
            mutable_location.pop("extensions")

        if "last_updated" not in location:
            _fail_missing_attribute("last_updated", key)
        last_updated = mutable_location.pop("last_updated")

        # Starlark doesn't have regexes.
        if len(last_updated) != 10 or last_updated[4] != "-" or last_updated[7] != "-":
            fail("last_updated must match YYYY-DD-MM: " + last_updated)

        if "cpe" in location:
            cpe = mutable_location.pop("cpe")

            # Starlark doesn't have regexes.
            cpe_components = len(cpe.split(":"))

            # We allow cpe:2.3:a:foo:* and cpe:2.3.:a:foo:bar:* only.
            cpe_components_valid = cpe_components in [5, 6]
            cpe_matches = (cpe == "N/A" or (cpe.startswith("cpe:2.3:a:") and cpe.endswith(":*") and cpe_components_valid))
            if not cpe_matches:
                fail("CPE must match cpe:2.3:a:<facet>:<facet>:*: " + cpe)
        elif not [category for category in USE_CATEGORIES_WITH_CPE_OPTIONAL if category in location["use_category"]]:
            _fail_missing_attribute("cpe", key)

        for category in location["use_category"]:
            if category not in USE_CATEGORIES:
                fail("Unknown use_category value '" + category + "' for dependecy " + key)

    return locations

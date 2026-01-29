load("@envoy_api//bazel:repository_locations_utils.bzl", "load_repository_locations_spec")

# Envoy dependencies may be annotated with the following attributes:
DEPENDENCY_ANNOTATIONS = [
    # Attribute specifying CPE (Common Platform Enumeration, see https://nvd.nist.gov/products/cpe) ID
    # of the dependency. The ID may be in v2.3 or v2.2 format, although v2.3 is prefferred. See
    # https://nvd.nist.gov/products/cpe for CPE format. Use single wildcard '*' for version and vector elements
    # i.e. 'cpe:2.3:a:nghttp2:nghttp2:*'. Use "N/A" for dependencies without CPE assigned.
    # This attribute is optional for components with use categories listed in the
    # USE_CATEGORIES_WITH_CPE_OPTIONAL
    "cpe",

    # A list of extensions when 'use_category' contains 'dataplane_ext' or 'observability_ext'.
    "extensions",

    # Additional dependencies loaded transitively via this dependency that are not tracked in
    # Envoy (see the external dependency at the given version for information).
    "implied_untracked_deps",

    # Project metadata.
    "project_desc",
    "project_name",
    "project_url",

    # Reflects the UTC date (YYYY-MM-DD format) for the dependency release. This
    # is when the dependency was updated in its repository. For dependencies
    # that have releases, this is the date of the release. For dependencies
    # without releases or for scenarios where we temporarily need to use a
    # commit, this date should be the date of the commit in UTC.
    "release_date",

    # List of the categories describing how the dependency is being used. This attribute is used
    # for automatic tracking of security posture of Envoy's dependencies.
    # Possible values are documented in the USE_CATEGORIES list below.
    # This attribute is mandatory for each dependecy.
    "use_category",

    # The dependency version. This may be either a tagged release (preferred)
    # or git SHA (as an exception when no release tagged version is suitable).
    "version",
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
    # Documentation generation
    "docs",
    # Developer tools (not used in build or docs)
    "devtools",
]

# Components with these use categories are not required to specify the 'cpe'.
USE_CATEGORIES_WITH_CPE_OPTIONAL = ["build", "other", "test_only", "api"]

def _fail_missing_attribute(attr, key):
    fail("The '%s' attribute must be defined for external dependency " % attr + key)

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

        if "project_desc" not in location:
            _fail_missing_attribute("project_desc", key)

        if "project_url" not in location:
            _fail_missing_attribute("project_url", key)
        project_url = location["project_url"]
        if not project_url.startswith("https://") and not project_url.startswith("http://"):
            fail("project_url must start with https:// or http://: " + project_url)

        if "version" not in location:
            _fail_missing_attribute("version", key)

        if "release_date" not in location:
            _fail_missing_attribute("release_date", key)
        release_date = location["release_date"]

        # Starlark doesn't have regexes.
        if len(release_date) != 10 or release_date[4] != "-" or release_date[7] != "-":
            fail("release_date must match YYYY-DD-MM: " + release_date)

        # Note: use_category, extensions, cpe, and other metadata fields are now in
        # separate YAML files (bazel/deps.yaml and api/bazel/deps.yaml) and are
        # validated in the merged JSON by downstream tools.

    return locations

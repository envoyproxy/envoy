#!/bin/bash

# This script generates a header file that is used by version_lib whenever linkstamp is not allowed.
# linkstamp is used to link in version_linkstamp.cc into the version_lib.
# However, linkstamp is not available to non-binary bazel targets.
# This means that if the topmost target being used to compile version_lib is a envoy_cc_library or related, linkstamp will not be in effect.
# In turn this means that version_linkstamp.cc is not linked, and the build_scm_revision and build_scm_status are unknown symbols to the linker.
build_scm_revision=$(grep BUILD_SCM_REVISION bazel-out/volatile-status.txt | sed 's/^BUILD_SCM_REVISION //' | tr -d '\\n')

echo "extern const char build_scm_revision[];"
echo "extern const char build_scm_status[];"
echo "const char build_scm_revision[] = \"$build_scm_revision\";"
echo "const char build_scm_status[] = \"Library\";"
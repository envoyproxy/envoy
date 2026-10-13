#!/usr/bin/env bash

# This script generates a header file that is used by version_lib whenever linkstamp is not allowed.
# linkstamp is used to link in version_linkstamp.cc into the version_lib.
# However, linkstamp is not available to non-binary bazel targets.
# This means that if the topmost target being used to compile version_lib is a envoy_cc_library or related, linkstamp will not be in effect.
# In turn this means that version_linkstamp.cc is not linked, and the build_scm_revision and build_scm_status are unknown symbols to the linker.

# Unfortunately linkstamp is not well documented (https://github.com/bazelbuild/bazel/issues/2893).
# But following the implicit trail one can deduce that linkstamp is in effect when "stamping" (https://github.com/bazelbuild/bazel/issues/2893) is on.
# envoy_cc_library -- and the underlying cc_library rule -- does not support "stamping".
# This makes sense as stamping mainly makes sense in the context of binaries for production releases, not static libraries.
build_scm_revision=$(sed -n -E 's/^BUILD_SCM_REVISION ([0-9a-f]{40})$/\1/p' < bazel-out/volatile-status.txt)
if [ -z "$1" ]; then
  build_scm_status=$(sed -n -E 's/^BUILD_SCM_STATUS ([a-zA-Z]*)$/\1/p' < bazel-out/volatile-status.txt)
else
  build_scm_status=$1
fi

echo "extern const char build_scm_revision[];"
echo "extern const char build_scm_status[];"
echo "const char build_scm_revision[] = \"$build_scm_revision\";"
echo "const char build_scm_status[] = \"$build_scm_status\";"

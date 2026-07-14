Bumped Bazel from 7 to 8. Bazel 8 disables WORKSPACE mode by default in favor of
bzlmod. Since Envoy still uses WORKSPACE, the flags ``--enable_workspace`` and
``--noenable_bzlmod`` are now required and have been added to ``.bazelrc``. External repo
runfiles are now placed directly under the runfiles root instead of nested under the
workspace name. Several dependency upgrades were needed, including ``rules_java`` and
``rules_android``.

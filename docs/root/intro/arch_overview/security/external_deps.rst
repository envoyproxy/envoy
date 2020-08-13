.. _arch_overview_external_deps:

External dependencies
=====================

Below we enumerate the external dependencies that may be linked into the Envoy binary.

Data plane dependencies:

.. include:: external_dep_dataplane.rst

Control plane dependencies:

.. include:: external_dep_controlplane.rst

Observability dependencies:

.. include:: external_dep_observability.rst

Test dependencies:

.. include:: external_dep_test.rst

Build dependencies:

.. include:: external_dep_build.rst

Miscellaneous dependencies:

.. include:: external_dep_other.rst

We exclude dependencies that only are used in CI or developer tooling above.

TODO: also analyze `api/bazel/repository_locations.py` and `bazel/dependency_imports.bzl`.

TODO: distinguish deps in core vs. extensions.

TODO: add last updated column.

TODO: integrate version into CPE links.

TODO: populate with external dep maturity status.

.. _arch_overview_external_deps:

External dependencies
=====================

Below we enumerate the external dependencies that may be linked into the Envoy binary. We exclude
dependencies that only are used in CI or developer tooling above.

Data plane (core)
-----------------

.. include:: external_dep_dataplane_core.rst

Data plane (extensions)
-----------------------

.. include:: external_dep_dataplane_ext.rst

Control plane
-------------

.. include:: external_dep_controlplane.rst

API
---

.. include:: external_dep_api.rst

Observability (core)
--------------------

.. include:: external_dep_observability_core.rst

Observability (extensions)
--------------------------

.. include:: external_dep_observability_ext.rst

Build
-----

.. include:: external_dep_build.rst

Miscellaneous
-------------

.. include:: external_dep_other.rst

Test only
---------

Below we provide the status of the C/C++ dependencies that are only used in tests. Tests also
include additional Java, Rust and Python dependencies that are not tracked below.

.. include:: external_dep_test_only.rst

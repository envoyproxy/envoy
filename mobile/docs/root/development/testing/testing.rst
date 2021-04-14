.. _testing:

Testing
=======

.. toctree::
  :maxdepth: 2

  local_stats

Envoy Mobile strives to have a high standard of code quality and test coverage for the entire
project.

All unit tests are in the various ``library/<language>`` directories of the repository,
and are run on CI for all PRs and merges to main.

Tests may be run locally as outlined below.

--------------------
Common (C/C++) tests
--------------------

To run the entire C/C++ test suite locally, use the following Bazel command:

``bazelisk test --test_output=all //test/common/...``

----------
Java tests
----------

To run the entire Java unit test suite locally, use the following Bazel command:

``bazelisk test --test_output=all --build_tests_only //test/java/...``

------------
Kotlin tests
------------

To run the entire Kotlin unit test suite locally, use the following Bazel command:

``bazelisk test --test_output=all --build_tests_only //test/kotlin/...``

-----------
Swift tests
-----------

To run the entire Swift unit test suite locally, use the following Bazel command:

``bazelisk test --config=ios --test_output=all --build_tests_only //test/swift/...``

--------
Coverage
--------

Currently, CI runs coverage jobs whenever any of the common (C/C++ code) under
``library/common/...`` or ``test/common/...`` changes; and on all commits to main.
The coverage job will fail if the resulting coverage percentage falls below the project's
`threshold <https://github.com/envoyproxy/envoy-mobile/blob/0b06697989c7d64ab73dee76744b7493ce38c28b/.github/workflows/coverage.yml#L23>`_.

The coverage report can be downloaded and inspected by clicking on the ``Artifacts`` drop down on
the top right corner of the coverage CI job.

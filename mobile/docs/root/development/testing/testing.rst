.. _testing:

Testing
=======

Envoy Mobile strives to have a high standard of code quality and test coverage for the entire
project.

All unit tests are in the various ``library/<language>`` directories of the repository,
and are run on CI for all PRs and merges to master.

Tests may be run locally as outlined below.

.. note::

  The overall project testing strategy is being revisited as part of :issue:`#197 <197>`.

----------
Java tests
----------

To run the entire Java unit test suite locally, use the following Bazel command:

``bazel test --test_output=all --build_tests_only //library/java/test/...``

------------
Kotlin tests
------------

To run the entire Kotlin unit test suite locally, use the following Bazel command:

``bazel test --test_output=all --build_tests_only //library/kotlin/test/...``

-----------
Swift tests
-----------

To run the entire Swift unit test suite locally, use the following Bazel command:

``bazel test --test_output=all --build_tests_only //library/swift/test/...``

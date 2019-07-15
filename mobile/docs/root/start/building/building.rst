.. _building:

Building
========

.. _building_requirements:

In order to compile the artifacts used by the Envoy Mobile library,
your system must also be set up for building Envoy. To get started, you can use
`this quick start guide
<https://github.com/envoyproxy/envoy/tree/master/bazel#quick-start-bazel-build-for-developers>`_.

Ensure that the ``envoy`` **submodule** is initialized when cloning by using ``--recursive``:

``git clone https://github.com/lyft/envoy-mobile.git --recursive``

If the repo was not initially cloned recursively, initialize the Envoy
submodule with ``git submodule update --init``.

------------------
Bazel requirements
------------------

Envoy-Mobile is compiled using Bazel 0.28.0.
Take a look at our CI set up for :repo:`mac <ci/mac_ci_setup.sh>` and :repo:`linux <ci/linux_ci_setup.sh>`,
in order to see how to install a specific Bazel version in your environment.

To assist with build consistency across environments, a `bazelw` script is included in project root, along with several common build configurations included in the project's `.bazelrc`.

--------------------
Android requirements
--------------------

- Android SDK Platform 28
- Android NDK 19.2.5345600

----------------
iOS requirements
----------------

- Xcode 10.2.1
- iOS 12.2 / Swift 5.0
- Note: Requirements are listed in the :repo:`.bazelrc file <.bazelrc>`

.. _android_aar:

-----------
Android AAR
-----------

Envoy Mobile can be compiled into an ``.aar`` file for use with Android apps.
This command is defined in the main :repo:`BUILD <BUILD>` file of the repo, and may be run locally:

``./bazelw build android_dist --config=android``

Upon completion of the build, you'll see an ``envoy.aar`` file at :repo:`dist/envoy.aar <dist>`.

Alternatively, you can use the prebuilt artifact from Envoy Mobile's releases_.
Download ``envoy-android-<platform>-v0.1.zip``, and place the unzipped contents
at :repo:`dist/envoy.aar <dist>`.

The ``envoy_mobile_android`` Bazel rule defined in the :repo:`dist BUILD file <dist/BUILD>` provides
an example of how this artifact may be used.

For a demo of a working app using this artifact, see the :ref:`hello_world` example.

.. _ios_framework:

--------------------
iOS static framework
--------------------

Envoy Mobile supports being compiled into a ``.framework`` directory for consumption by iOS apps.
This command is defined in the main :repo:`BUILD <BUILD>` file of the repo, and may be run locally:

``./bazelw build ios_dist --config=ios``

Upon completion of the build, you'll see a ``Envoy.framework`` directory at
:repo:`dist/Envoy.framework <dist>`.

Alternatively, you can use the prebuilt artifact from Envoy Mobile's releases_.
Download ``envoy-ios-macos-v0.1.zip``, and place the unzipped contents at :repo:`dist/Envoy.framework <dist>`.

The ``envoy_mobile_ios`` Bazel rule defined in the :repo:`dist BUILD file <dist/BUILD>` provides an
example of how this artifact may be used.

For a demo of a working app using this artifact, see the :ref:`hello_world` example.

.. _releases: https://github.com/lyft/envoy-mobile/releases

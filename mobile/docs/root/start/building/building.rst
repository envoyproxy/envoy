.. _building:

Building
========

.. _building_requirements:

To build Envoy Mobile, your system must also be set up for building Envoy.
To get started, you can use `this quick start guide
<https://github.com/envoyproxy/envoy/tree/master/bazel#quick-start-bazel-build-for-developers>`_.

Ensure that the ``envoy`` **submodule** is initialized when cloning by using ``--recursive``:

``git clone https://github.com/lyft/envoy-mobile.git --recursive``

If the repo was not initially cloned recursively, you can manually initialize the Envoy submodule:

``git submodule update --init``

------------------
Bazel requirements
------------------

Envoy Mobile is compiled using the version of Bazel specified in the
:repo:`.bazelversion <.bazelversion>` file.

To simplify build consistency across environments, a :repo:`bazelw <bazelw>` script is included in
the project, along with common build configurations in the project's :repo:`.bazelrc <.bazelrc>`.

--------------------
Android requirements
--------------------

- Android SDK Platform 28
- Android NDK 19.2.5345600

----------------
iOS requirements
----------------

- Xcode 11.1.0
- Swift 5.0
- Note: Requirements are listed in the :repo:`.bazelrc file <.bazelrc>` and CI scripts

.. _android_aar:

-----------
Android AAR
-----------

Envoy Mobile can be compiled into an ``.aar`` file for use with Android apps.
This command is defined in the main :repo:`BUILD <BUILD>` file of the repo, and may be run locally:

``./bazelw build android_dist --config=android --config=release # omit release for development``

Upon completion of the build, you'll see an ``envoy.aar`` file at :repo:`dist/envoy.aar <dist>`.

Alternatively, you can use the prebuilt artifact from Envoy Mobile's releases_.

The ``envoy_mobile_android`` Bazel rule defined in the :repo:`dist BUILD file <dist/BUILD>` provides
an example of how this artifact may be used.

When building the artifact for release (usage outside of development), be sure to include the
``--config=release`` option.

For a demo of a working app using this artifact, see the :ref:`hello_world` example.

.. _ios_framework:

--------------------
iOS static framework
--------------------

Envoy Mobile supports being compiled into a ``.framework`` for consumption by iOS apps.
This command is defined in the main :repo:`BUILD <BUILD>` file of the repo, and may be run locally:

``./bazelw build ios_dist --config=ios --config=release # omit release for development``

Upon completion of the build, you'll see a ``Envoy.framework`` directory at
:repo:`dist/Envoy.framework <dist>`.

Alternatively, you can use the prebuilt artifact from Envoy Mobile's releases_.

The ``envoy_mobile_ios`` Bazel rule defined in the :repo:`dist BUILD file <dist/BUILD>` provides an
example of how this artifact may be used.

When building the artifact for release (usage outside of development), be sure to include the
``--config=release`` option in addition to ``--config=ios`` and a list of architectures for which
you wish to build using ``--ios_multi_cpus=...``.

For a demo of a working app using this artifact, see the :ref:`hello_world` example.

.. _releases: https://github.com/lyft/envoy-mobile/releases

---------
CocoaPods
---------

If you use CocoaPods, you can add the following to your ``Podfile`` to use the latest version of the
prebuilt Envoy Mobile framework.

``pod 'EnvoyMobile'``

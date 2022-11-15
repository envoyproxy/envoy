.. _building:

Building
========

To build Envoy Mobile, your system must also be set up for building Envoy.
To get started, you can use `this quick start guide
<https://github.com/envoyproxy/envoy/tree/master/bazel#quick-start-bazel-build-for-developers>`_.

Ensure that the ``envoy`` **submodule** is initialized when cloning by using ``--recursive``:

``git clone https://github.com/envoyproxy/envoy-mobile.git --recursive``

If the repo was not initially cloned recursively, you can manually initialize the Envoy submodule:

``git submodule update --init``

.. _releases: https://github.com/envoyproxy/envoy-mobile/releases

------------------
Bazel requirements
------------------

Envoy Mobile is compiled using the version of Bazel specified in the
:repo:`.bazelversion <.bazelversion>` file.

To simplify build consistency across environments, the `./bazelw` script manages
using the correct version. Instead of using `bazel build ...` use `./bazelw build ...`
for all bazel commands.

--------------------
Java requirements
--------------------

- Java 8

Make sure that Java 8 set on `PATH` and that `JAVA_HOME` is set to the appropriate SDK.

--------------------
Android requirements
--------------------

- Android SDK Platform 30
- Android NDK 21

For local builds, set ``ANDROID_HOME`` and ``ANDROID_NDK_HOME`` to point to the location of these installs. For example,

.. code-block:: bash

  ANDROID_HOME=$HOME/Library/Android/sdk
  ANDROID_NDK_HOME=$HOME/Library/Android/ndk/21.3.6528147

See `ci/mac_ci_setup.sh` for the specific NDK version used during builds.

.. _ios_requirements:

----------------
iOS requirements
----------------

- Xcode 14.0
- iOS 12.0 or later
- Note: Requirements are listed in the :repo:`.bazelrc file <.bazelrc>` and CI scripts

.. _android_aar:

-----------
Android AAR
-----------

Envoy Mobile can be compiled into an ``.aar`` file for use with Android apps.
This command is defined in the main :repo:`BUILD <BUILD>` file of the repo, and may be run locally:

``./bazelw build android_dist --config=android --fat_apk_cpu=<arch1,arch2>``

Upon completion of the build, you'll see an ``envoy.aar`` file at :repo:`bazel-bin/library/kotlin/io/envoyproxy/envoymobile/envoy.aar`.

Alternatively, you can use the **prebuilt artifact** from Envoy Mobile's releases_
or from :ref:`Maven <maven>`.

The ``envoy_mobile_android`` Bazel rule defined in the :repo:`root BUILD file <BUILD>` provides
an example of how this artifact may be used.

**When building the artifact for release** (usage outside of development), be sure to include the
``--config=release-android`` option, along with the architectures for which the artifact is being built:

``./bazelw build android_dist --config=release-android --fat_apk_cpu=x86,armeabi-v7a,arm64-v8a``

For a demo of a working app using this artifact, see the :ref:`hello_world` example.

.. _ios_framework:

--------------------
iOS static framework
--------------------

Envoy Mobile supports being compiled into a ``.framework`` for consumption by iOS apps.
This command is defined in the main :repo:`BUILD <BUILD>` file of the repo, and may be run locally:

``./bazelw build ios_dist --config=ios``

Upon completion of the build, you'll see a ``ios_framework.zip`` file at output in a path bazel picks.

Alternatively, you can use the prebuilt artifact from Envoy Mobile's releases_ (Envoy.xcframework.zip)
or from :ref:`SwiftPM <swiftpm>`.

**When building the artifact for release** (usage outside of development), be sure to include the
``--config=release-ios`` option, along with the architectures for which the artifact is being built:

``./bazelw build ios_dist --config=release-ios --ios_multi_cpus=i386,x86_64,armv7,arm64``

For a demo of a working app using this artifact, see the :ref:`hello_world` example.

.. _maven:

-----
Maven
-----

Envoy Mobile Android artifacts are also uploaded to Maven, and can be accessed/downloaded
`here <https://mvnrepository.com/artifact/io.envoyproxy.envoymobile/envoy>`_.

.. _swiftpm:

---------------------
Swift Package Manager
---------------------

If you use the Swift Package Manager on iOS, you can add the following to your ``Package.swift`` to
use a version of the prebuilt Envoy Mobile framework.

.. code-block:: swift

  .binaryTarget(
    name: "Envoy",
    url: "https://github.com/envoyproxy/envoy-mobile/releases/download/<version>/Envoy.xcframework.zip",
    checksum: "..."
  )


---------------------------------------------
Building Envoy Mobile with private Extensions
---------------------------------------------

Similar to Envoy, Envoy Mobile has bazel targets that allows the library to be built as a git
submodule in a consuming project. This setup enables creating private extensions, such as filters.

~~~~~~~~~~
Extensions
~~~~~~~~~~

The top-level `envoy_build_config` directory allows Envoy Mobile to tap into Envoy's already
existing `selective extensions system <https://github.com/envoyproxy/envoy/blob/master/bazel/README.md#disabling-extensions>`_.

.. attention::

  Envoy Mobile requires force registration
  of extensions in the extension_registry.cc/h files due to static linking.
  For example, installing the XffIpDetection extension in this `PR <https://github.com/envoyproxy/envoy-mobile/pull/1481/files#diff-267d81747f176dadc207207f586f1924c0d472d182a5ba041c077454764b4449>`_.

In order to override the extensions built into Envoy Mobile create an ``envoy_build_config`` directory
and include the following in the WORKSPACE file::

  local_repository(
    name = "envoy_build_config",
    # Relative paths are also supported.
    path = "/somewhere/on/filesystem/envoy_build_config",
  )

------------------------------
Deploying Envoy Mobile Locally
------------------------------

~~~~~~~
Android
~~~~~~~

To deploy Envoy Mobile's aar to your local maven repository, run the following commands::

    # To build Envoy Mobile. --fat_apk_cpu takes in a list of architectures: [x86|armeabi-v7a|arm64-v8a].
    ./bazelw build android_dist --config=android --fat_apk_cpu=x86

    # To publish to local maven.
    ci/sonatype_nexus_upload.py --local --files bazel-bin/library/kotlin/io/envoyproxy/envoymobile/envoy.aar bazel-bin/library/kotlin/io/envoyproxy/envoymobile/envoy-pom.xml


The version deployed will be ``LOCAL-SNAPSHOT``. These artifacts can be found in your local maven directory (``~/.m2/repository/io/envoyproxy/envoymobile/envoy/LOCAL-SNAPSHOT/``)

~~~
iOS
~~~
TODO :issue:`#980 <980>`

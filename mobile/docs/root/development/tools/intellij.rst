.. _intellij_development:

IntelliJ Development
====================

`bazelbuild/intellij <https://github.com/bazelbuild/intellij>`_ is an IntelliJ plugin for Bazel projects.

Using IntelliJ with Envoy Mobile
--------------------------------

To get started using IntelliJ with Envoy Mobile:

1. Download a supported `IntelliJ version <https://www.jetbrains.com/idea/download/other.html>`_ supported by the Bazel plugin
2. Apply local hacks to make IntelliJ work using the branch `hack-for-intellij <https://github.com/envoyproxy/envoy-mobile/tree/hack-for-intellij>`_
3. Open up the Envoy Mobile project using the Bazel import project wizard


Known issues
------------

1. IntelliJ is unable to find the Android SDK/NDK when using hermetic toolchains. The SDK and NDK are fetched automatically by Bazel; no ``ANDROID_HOME`` or ``ANDROID_NDK_HOME`` environment variables need to be set.
2. Ongoing issues related to the `Bazel plugin <https://github.com/bazelbuild/intellij/issues/529>`_ so we'll update :tree:`.bazelrc <57fb4d405d11c89f028b10e6e00c7b5aa3d8ddd2/.bazelrc#L4>` to set `--incompatible_depset_is_not_iterable`

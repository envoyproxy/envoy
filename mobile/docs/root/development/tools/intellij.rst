.. _intellij_development:

IntelliJ Development
====================

`bazelbuild/intellij <https://github.com/bazelbuild/intellij>`_ is an IntelliJ plugin for Bazel projects.

Using IntelliJ with Envoy Mobile
--------------------------------

To get started using IntelliJ with Envoy Mobile:

1. Locally install `Bazel <https://docs.bazel.build/versions/master/install-os-x.html#install-on-mac-os-x-homebrew>`_
2. Download a supported `IntelliJ version <https://www.jetbrains.com/idea/download/other.html>`_ supported by the Bazel plugin
3. Apply local hacks to make IntelliJ work using the branch `hack-for-intellij <https://github.com/lyft/envoy-mobile/tree/hack-for-intellij>`_
4. Open up the Envoy Mobile project using the Bazel import project wizard


Known issues
------------

1. IntelliJ is unable to find the appropriate `ANDROID_HOME` and `ANDROID_NDK_HOME` which is the reason we need to hard code it :tree:`locally <cdf8353b126590ef9369883ca9eba85613c81bdc/WORKSPACE#L94-L96>`
2. Ongoing issues related to the `Bazel plugin <https://github.com/bazelbuild/intellij/issues/529>`_ so we'll update :tree:`.bazelrc <57fb4d405d11c89f028b10e6e00c7b5aa3d8ddd2/.bazelrc#L4>` to set `--incompatible_depset_is_not_iterable`

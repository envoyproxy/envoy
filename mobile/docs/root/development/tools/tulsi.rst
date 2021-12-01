.. _tulsi_development:

Tulsi Development
=================

`Tulsi <https://github.com/bazelbuild/tulsi>`_ is a tool that aims to integrate
Bazel with Xcode in order to generate, build, and run Bazel-based projects with
the Xcode IDE which is commonly used for Apple/iOS development.

Configuration files for Tulsi are checked into the Envoy Mobile repository in order to:

- Provide a way to generate Xcode projects to build the Envoy Mobile library
- Reduce barrier to entry for iOS engineers hoping to contribute

Using Tulsi with Envoy Mobile
-----------------------------

To get started using Tulsi with Envoy Mobile:

1. Download and `install Tulsi <https://tulsi.bazel.build/docs/gettingstarted.html>`_
2. Open the :repo:`envoy-mobile.tulsiproj <envoy-mobile.tulsiproj>` file
3. From the ``Packages`` tab, click ``Bazel..`` and select the ``bazelw`` binary from at the root of the Envoy Mobile directory (to ensure you're building with the correct version of Bazel)
4. Click on the ``Configs`` tab in Tulsi, and click ``Generate``
5. Open up the Xcode project, and build

Known issues
------------

Code completion for Swift for Tulsi-based project is currently broken.
It's being tracked in
`Tulsi issue #96 <https://github.com/bazelbuild/tulsi/issues/96>`_.

.. _debugging_ios_instructions:

Build & run the example iOS apps
=======================================

*Note: This document assumes that you have installed the
:ref:`iOS build requirements <ios_requirements>`_.*

The fastest way to build and run the sample iOS apps is to run the
following command::

    ./bazelw run //examples/swift/hello_world:app

This will build and run the Hello World iOS app in a new iOS Simulator.

.. _using_xcode:

Using the Xcode GUI
-------------------

Envoy Mobile makes use of the
`rules_xcodeproj <https://github.com/buildbuddy-io/rules_xcodeproj>`_
project to add support for many of Xcode's development, debugging and
profiling features to Envoy Mobile.

To start, run ``./bazelw run //:xcodeproj`` to generate an Xcode project
and ``xed .`` to open it in Xcode (or double-click ``Envoy.xcodeproj``
in Finder).

In Xcode's scheme selector, pick the app target you want to build (e.g.
``__examples_swift_hello_world_app``), pick a Simulator to run it on,
then hit cmd-R to build and run.

From there, most Xcode features should just work for all transitively
compiled source files in C/C++/Objective-C/Objective-C++/Swift:

* Auto-complete
* Syntax highlighting
* Go to definition
* Breakpoints
* LLDB console
* Thread navigator
* Symbols
* Metrics like CPU/memory/networking/energy
* Profiling with Instruments

|xcode_breakpoint| |instruments|

.. |xcode_breakpoint| image:: images/xcode_breakpoint.jpg
   :width: 45%

.. |instruments| image:: images/instruments.jpg
   :width: 45%

Running on a real iPhone
------------------------

Although building and running on a physical device is
`not yet officially supported <https://github.com/buildbuddy-io/rules_xcodeproj/issues/285>`_
by rules_xcodeproj, following these steps may work:

1. Add ``build --ios_multi_cpus=arm64`` to ``.bazelrc``.
2. Add a ``.mobileprovision`` provisioning profile capable of signing
   the app you want to run next to its ``BUILD`` file.
   E.g. ``examples/swift/hello_world/dev.mobileprovision``
3. Set the provisioning profile on the ``ios_application`` you want to
   run. E.g. ``provisioning_profile = "dev.mobileprovision",``
4. Remove all apps without a provisioning profile from the ``xcodeproj``
   configuration in Envoy Mobile's root ``BUILD`` file.
5. Follow the same steps as defined in the
   :ref:`Using the Xcode GUI <using_xcode>`_ section above, but
   targeting your device instead of a simulator.

*Note: You may need to clean from Xcode with cmd-shift-k between device
runs.*

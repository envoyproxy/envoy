.. _dev_performance_device_conditions:

Analysis of device conditions
=============================

Results
~~~~~~~

iOS
---

Valid through SHA :tree:`f05d43f <f05d43f>`.

We did not observe any issues when switching between background/foreground or between WiFi/cellular.
**However**, we believe that this will become problematic when we change to calling Envoy directly,
rather making requests through ``URLSession`` and having Envoy proxy them.
This test will need to be re-run after those changes. See below for more details.

Android
-------

Valid through SHA :tree:`e85e553 <e85e553>`.

We did not observe any issues when switching between background/foreground or between WiFi/cellular.
Network requests resumed and were successful after a brief period of time.

Experimentation method
~~~~~~~~~~~~~~~~~~~~~~

Modified versions of the "hello world" example apps were used to run the following experiments.
See the iOS/Android sections below for instructions on building the examples.

Throughout all of the following steps, we tested to make sure that network requests succeeded
when making each of the lifecycle/network changes listed below.

Lifecycle experiment steps:

1. Open the example application
2. Background the application
3. Foreground the application

Network experimentation steps:

1. Turn on WiFi
2. Open the example application
3. Turn off WiFi
4. Turn on WiFi
5. Turn on airplane mode
6. Turn off airplane mode

iOS
---

The original investigation was completed as part of :issue:`this issue <128>`.

Reproducing the Envoy example app:

1. Build the library using ``bazel build ios_dist --config=ios --config=fat``
2. Copy ``./dist/Envoy.framework`` to the example's :repo:`source directory <examples/objective-c/hello_world>`
3. Build/run the example app on a physical device

Android
-------

Reproducing the Envoy example app:

1. Build the library using ``bazel build android_dist --config=android``
2. Run the app: ``bazel mobile-install //examples/kotlin/hello_world:hello_envoy_kt --fat_apk_cpu=armeabi-v7a``

Analysis
~~~~~~~~

iOS
---

With the current configuration of sending traffic over ``URLSession`` and having Envoy proxy it through,
there seems to be no issues on iOS when switching between WiFi/cellular or background/foreground.

However, these findings are strange given that
`issues have been observed <https://github.com/grpc/grpc-swift/issues/337>`_ with the cellular radio and gRPC
in the past. We were able to reproduce this issue in the :issue:`investigation <128>`, which showed that gRPC
channels (using BSD sockets under the hood) stalled when switching from WiFi to cellular, while requests made
through ``URLSession`` and proxied through Envoy continued to succeed.

The current theory is that ``URLSession`` is doing something smart under the hood to handle these changes.

We will need to re-run these tests when we switch to calling Envoy Mobile directly as a library
(rather than running on top of calls to ``URLSession``).

Depending on the outcome of :issue:`#13 <13>`, we shouldn't have problems as long as we use Apple-approved
network solutions for the transport on iOS (such as ``CFNetwork``/``Network.framework``/etc.).

Android
-------

The initial experiment was done purely by looking at the results shown on the UI in the example application. The requests
succeed after some time. To be certain that what we observed in the high level experiment were valid, we enabled ``trace``
level logging within Envoy to ensure Envoy is getting the requests back.


Open issues regarding device conditions
---------------------------------------

For current issues with device conditions, please see issues with the
`perf/device label <https://github.com/lyft/envoy-mobile/labels/perf%2Fdevice>`_.

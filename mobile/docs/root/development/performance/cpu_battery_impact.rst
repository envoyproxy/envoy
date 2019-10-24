.. _dev_performance_cpu_battery:

Analysis of CPU/battery impact
==============================

.. warning::

  These analyses have not been updated since Envoy Mobile added interfaces for performing
  network requests directly using the library. They will be re-run as part of :issue:`#536 <536>`.

Modified versions of the "hello world" example apps were used to run these experiments:

- :tree:`Android control app <8636711/examples/kotlin/control>`
- :tree:`Android Envoy app <8636711/examples/kotlin/hello_world>`
- :tree:`iOS control app <2f27581/examples/objective-c/control/control>`
- :tree:`iOS Envoy app <2f27581/examples/objective-c/xcode_variant/EnvoyObjc/EnvoyObjc>`

The 2 apps on each platform:

- **Control:** Made a request every ``200ms`` to an endpoint without Envoy compiled in the app.
- **Envoy:** Made the same request at the same interval, but routed through an instance of Envoy.

All request/response caching was disabled.

Results
~~~~~~~

iOS
---

Valid through SHA :tree:`2f27581 <2f27581>`.

Envoy:

- Avg CPU: ~4%
- Avg memory: 12MB
- Battery: 1/20 Xcode Instruments score

Control:

- Avg CPU: ~2%
- Avg memory: 6MB
- Battery: 1/20 Xcode Instruments score

**Based on these results, control and Envoy are relatively similar with a slight increase using Envoy.**

Android
-------

Valid through SHA :tree:`8636711 <8636711>`.

Envoy:

- Avg CPU: 33.16075949%
- Avg memory: 2.765822785%
- Battery: 0.17%/min

Control:

- Avg CPU: 28.81012658%
- Avg memory: 2.169620253%
- Battery: 0.18%/min

**Based on these results, control and Envoy are relatively similar.**

Experimentation method
~~~~~~~~~~~~~~~~~~~~~~

iOS
---

The original investigation was completed as part of :issue:`#113 <113>`,
and a critical performance issue was fixed in :issue:`#215 <215>`.

For analysis, the `Energy Diagnostics tool from Xcode Instruments <https://developer.apple.com/library/archive/documentation/Performance/Conceptual/EnergyGuide-iOS/MonitorEnergyWithInstruments.html>`_
was used.

Requests were made using ``URLSession``, with the session's cache set to ``nil`` (disabling caching).
Envoy listened to the data sent over ``URLSession``, proxying it through.

Both apps were run (one at a time) on a physical device (iPhone 6s iOS 12.2.x) while running Instruments.

Reproducing the Envoy example app:

1. Build the library using ``bazel build ios_dist --config=ios --config=fat``
2. Copy ``./dist/Envoy.framework`` to the example's :tree:`source directory <2f27581/examples/objective-c/xcode_variant/EnvoyObjc/EnvoyObjc>`
3. Build/run the example app

Android
-------

We're currently using ``HttpURLConnection`` to communicate and send requests to Envoy. Envoy in it's current state is run as
a process listening to traffic sent over this connection.

Getting the build:

1. Build the library using ``bazel build android_dist --config=android``
2. Control: ``bazel mobile-install //examples/kotlin/control:hello_control_kt``
3. Envoy: ``bazel mobile-install //examples/kotlin/hello_world:hello_envoy_kt --fat_apk_cpu=armeabi-v7a``

Battery usage experiment steps:

1. Set a phone's display to sleep after 30 minutes of inactivity
2. Unplug the phone from all power sources
3. Open up the demo app
4. Wait for the phone to sleep
5. Look at the battery drain the battery settings in the phone to see the battery usage and drainage

Alternative profiling methods tried:

1. `AccuBattery <https://play.google.com/store/apps/details?id=com.digibites.accubattery&hl=en_US>`_:
We were unable to get the running time of a given application on AccuBattery to more accurately identify battery usage per minute

2. `Battery Historian <https://github.com/google/battery-historian>`_:
We were unable to get reliable data using this method. Often times, the battery usage of an application appears to use no batteries

CPU usage experiment steps:

1. Run ``adb shell top -H | grep envoy`` to get the CPU usage of the application (the ``-H`` flag displays the running threads)
2. Wait 10minutes to gather a sample set of data to analyze
3. Take the average CPU% and MEM%

Analysis
~~~~~~~~

iOS
---

Envoy had a small increase in memory and CPU usage compared to control.

During the :issue:`initial investigation <113#issuecomment-505676324>`, we identified and fixed
:issue:`issue <215>` with ``libevent`` that was severely degrading CPU (and subsequently battery) performance.

:issue:`We used Wireshark <113#issuecomment-505673869>` to validate that
network traffic was flowing through Envoy on the phone every ``200ms``, giving us confidence that there was
no additional caching happening within ``URLSession``.

Android
-------

There are minimal differences between Envoy and control. By enabling trace logging within Envoy,
we are able to observe the following:

1. Requests to S3 are being logged in Envoy
2. DNS resolution does happen every 5 seconds
3. Stats are flushed every 5 seconds

The DNS resolution and stats flush happening every 5 seconds was originally a concern,
but updating the frequency to 1 minute did not result in a significant change.

Open issues regarding battery usage
-----------------------------------

For current issues with CPU/battery, please see issues with the
`perf/cpu label <https://github.com/lyft/envoy-mobile/labels/perf%2Fcpu>`_.

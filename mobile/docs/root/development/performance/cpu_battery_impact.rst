.. _dev_performance_cpu_battery:

Analysis of CPU/battery impact
==============================

We utilized 2 apps (1 control, 1 variant) on each platform to validate
performance using the native networking stacks versus Envoy Mobile:

- **Control:** Made a request every ``200ms`` to an endpoint without Envoy compiled in the app.
- **Envoy:** Made the same request at the same interval, but using Envoy Mobile.

All request/response caching was disabled.

Results
~~~~~~~

iOS
---

Valid through SHA :tree:`v0.2.3.03062020 <v0.2.3.03062020>`.

Envoy:

- Avg CPU: ~4%
- Avg memory: 46MB
- Battery: Negligible

Control:

- Avg CPU: ~4%
- Avg memory: 29MB
- Battery: Negligible

**Based on these results, control and Envoy are relatively similar with a slight increase in memory using Envoy.**

Android
-------

Valid through SHA :tree:`v0.3.0.05122020 <v0.3.0.05122020>`.

Envoy:

- Avg CPU: ~5.16%
- Avg memory: 109MB
- Battery: 0.2%/min

Control:

- Avg CPU: ~2.94%
- Avg memory: 52MB
- Battery: 0.23%/min

**Based on these results, control and Envoy are relatively similar.**

Experimentation method
~~~~~~~~~~~~~~~~~~~~~~

iOS
---

The sample apps checked into
`this analysis repository <https://github.com/rebello95/EnvoyMobileAnalysis/tree/v0.2.3.03062020>`_
were used to run the analysis outlined in this document.

For the analysis, we utilized Xcode Instruments to monitor the 2 sample apps
while they ran in the foreground for a few minutes.

Control requests were made using ``URLSession``, with the session
configuration's cache set to ``nil`` (disabling caching).

Variant requests were made using the ``EnvoyClient``.

Both apps were run (one at a time) on a physical device (iPhone XS, iOS 13.3.1)
while running Instruments.

Additional screenshots from the analysis are available on the
`release <https://github.com/rebello95/EnvoyMobileAnalysis/releases/tag/v0.2.3.03062020>`_.

Android
-------

Modified versions of the "hello world" example apps were used to run these experiments:

- :tree:`Android control app <8636711/examples/kotlin/control>`
- :tree:`Android Envoy app <8636711/examples/kotlin/hello_world>`

Getting the build:

1. Build the library using ``./bazelw build android_dist --config=android --fat_apk_cpu=armeabi-v7a``
2. Control: ``./bazelw mobile-install //examples/kotlin/control:hello_control_kt``
3. Envoy: ``./bazelw mobile-install //examples/kotlin/hello_world:hello_envoy_kt --fat_apk_cpu=armeabi-v7a``

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

Open issues
~~~~~~~~~~~

For current issues with CPU/battery, please see issues with the
`perf/cpu label <https://github.com/envoyproxy/envoy-mobile/labels/perf%2Fcpu>`_.

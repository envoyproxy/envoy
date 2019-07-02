.. _dev_performance_device_conditions:

Analysis of device conditions
=============================

Results
~~~~~~~

iOS
---

* TODO

Android
-------

We did not observe any issues when switching between background/foreground and between WiFi/cellular. Network requests
resumed and were successful after a brief period of time.

Experimentation method
~~~~~~~~~~~~~~~~~~~~~~

iOS
---

* TODO

Android
-------

Getting the build:

1. Build the library using ``bazel build android_dist --config=android``
2. Control: ``bazel mobile-install //examples/kotlin/control:hello_control_kt``
3. Envoy: ``bazel mobile-install //examples/kotlin/hello_world:hello_envoy_kt --fat_apk_cpu=armeabi-v7a``

Lifecycle experiment steps:

1. Open the example application
2. Background the application
3. Foreground the application

Network experimentation steps:

1. Turn on WiFi
2. Open the example application
3. Turn off WiFi
4. Turn on WiFI
5. Turn on airplane mode
6. Turn off airplane mode


Analysis
~~~~~~~~

iOS
---

* TODO

Android
-------

The initial experiment was done purely by looking at the results shown on the UI in the example application. The requests
succeed after some time. To be certain that what we observed in the high level experiment were valid, we enabled ``trace``
level logging within Envoy to ensure Envoy is getting the requests back.

Current status
~~~~~~~~~~~~~~

As of
https://github.com/lyft/envoy-mobile/tree/e85e553a2a6a6a552460110f421dda6cac7e5b82

This test was done with Envoy being run as a process and using native APIs to make requests to Envoy.

Open issues regarding device conditions
---------------------------------------

Current status
~~~~~~~~~~~~~~

.. _dev_performance_cpu:

.. _ios_envoy_example_app: https://github.com/lyft/envoy-mobile/tree/ac/envoy-battery-cpu-branch/examples/swift/hello_world
.. _android_envoy_example_app: https://github.com/lyft/envoy-mobile/tree/ac/envoy-battery-cpu-branch/examples/kotlin/hello_world
.. _android_envoy_example_control_app: https://github.com/lyft/envoy-mobile/tree/ac/envoy-battery-cpu-branch/examples/kotlin/control

Analysis of CPU impact
======================

Experimentation method
~~~~~~~~~~~~~~~~~~~~~~

iOS
---

* TODO

Android
-------

We're currently using HttpURLConnection to communicate and send requests to Envoy every 200ms.
Envoy in it's current state is run as
a process

Getting the build:

1. Build the library using ``bazel build android_dist --config=android``
2. Control: ``bazel mobile-install //examples/kotlin/control:hello_control_kt``
3. Envoy: ``bazel mobile-install //examples/kotlin/hello_world:hello_envoy_kt --fat_apk_cpu=armeabi-v7a``

Experiment steps:

1. Run ``adb shell top -H | grep envoy`` to get the CPU usage of the application (the ``-H`` flag displays the running threads)
2. Wait 10minutes to gather a sample set of data to analyze
3. Take the average CPU% and MEM%

Results
~~~~~~~

iOS
---

* TODO

Android
-------

Envoy:

- Avg CPU: 33.16075949%
- Avg MEM: 2.765822785%

Control:

- Avg CPU: 28.81012658%
- Avg MEM: 2.169620253%

Analysis
~~~~~~~~

iOS
---

* TODO

Android
-------

The results of this experiment is that there is minimal difference between Envoy and Control. By enabling trace logging
within Envoy, we are able to observe the following:

1. Requests to s3 are being logged in Envoy
2. DNS resolution does happen every 5 seconds
3. Stats are flushed every 5 seconds

Open issues regarding battery usage
-----------------------------------

Current status
~~~~~~~~~~~~~~

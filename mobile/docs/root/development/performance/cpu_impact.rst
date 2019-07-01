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

Getting the build:
1. Build the library using ``bazel build android_dist --config=android``
2. Control: ``bazel mobile-install //examples/kotlin/control:hello_control_kt``
3. Envoy: ``bazel mobile-install //examples/kotlin/hello_world:hello_envoy_kt --fat_apk_cpu=armeabi-v7a``

Experiment steps:
* TODO

Results
~~~~~~~

iOS
---

* TODO

Android
-------

* TODO

Analysis
~~~~~~~~

iOS
---

* TODO

Android
-------

* TODO

Open issues regarding battery usage
-----------------------------------

Current status
~~~~~~~~~~~~~~

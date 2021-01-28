.. _dev_performance_connectivity:

Device connectivity analysis
============================

~~~~~~~
Results
~~~~~~~

Envoy Mobile currently handles switching preferred networks between wifi/cellular based on
reachability updates from the OS. After switching to a new preferred network, all future requests
made through the library will use the new connection.

---
iOS
---

The above approach has proven successful for unary requests, but has been problematic for long-lived
streams due to the fact that the library does not aggressively shut down these streams when the
preferred network changes, causing them to sometimes hang until they time out.

.. note::

  Issues :issue:`#541 <541>` and :issue:`#13 <13>` are being used to track the behavior of
  long-lived streams, as well as using native platform sockets to alleviate the issues above.

-------
Android
-------

We did not observe any issues when switching between background/foreground or between wifi/cellular.

~~~~~~~~~~~~~~~~~~~~~~
Experimentation method
~~~~~~~~~~~~~~~~~~~~~~

Modified versions of the "hello world" example apps were used to run the following experiments,
validating that the library is able to continue making successful requests after each change.

Lifecycle experiment steps:

1. Open the example application
2. Background the application
3. Foreground the application

Network experimentation steps:

1. Turn on wifi
2. Open the example application
3. Turn off wifi
4. Turn on wifi
5. Turn on airplane mode
6. Turn off airplane mode

-----------------
iOS configuration
-----------------

1. Build the library with debugging symbols (using ``--copt=-ggdb3``)

2. Add the outputted ``Envoy.framework`` to the example app

3. In the active scheme of the app's Xcode ``Environment Variables``, set ``CFNETWORK_DIAGNOSTICS=3`` to enable more verbose ``CFNetwork`` logs

4. Set Envoy's logs to ``trace``

---------------------
Android configuration
---------------------

1. Build the library

2. Build and run the example app:

``bazelisk mobile-install //examples/kotlin/hello_world:hello_envoy_kt --fat_apk_cpu=armeabi-v7a``

~~~~~~~~~~~
Open issues
~~~~~~~~~~~

For current issues with device conditions, please see issues with the
`perf/device label <https://github.com/lyft/envoy-mobile/labels/perf%2Fdevice>`_.

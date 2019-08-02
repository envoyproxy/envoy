.. _dev_performance_device_conditions:

Analysis of device conditions
=============================

Results
~~~~~~~

iOS
---

Valid through SHA :tree:`f05d43f <f05d43f>`.

.. warning::

  Envoy Mobile is currently unable to properly reconnect or switch between connections when the device
  is switched between WiFi and cellular. This is a common issue with libraries that use BSD sockets,
  and it is being fixed as part of :issue:`this issue <13>`.

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

iOS
---

The original investigation was completed as part of :issue:`this issue <128#issuecomment-516260951>`.

**Configuration:**

1. Build the app using the following flags to allow us to build to a device with debugging symbols:

``bazel build ios_dist --config=ios --ios_multi_cpus=arm64 --copt=-ggdb3``

2. Add the outputted ``Envoy.framework`` to the example app

3. In the active scheme of the app's Xcode ``Environment Variables``, set ``CFNETWORK_DIAGNOSTICS=3`` to enable more verbose ``CFNetwork`` logs

4. Set Envoy's logs to ``trace``

**Experiment:**

Make single requests to a
`simple Python server <https://github.com/Reflejo/TestNetworking/blob/master/TestNetworking/client.c>`_
that shows when the client connects, disconnects, and makes requests. These request should be routed
through Envoy **using the socket implementation of Envoy Mobile via URLSession**
(not calling directly into the Envoy Mobile library).

- Send a request on WiFi, then switch to cellular (disabling WiFi) and make more requests
- Set ``URLSessionConfiguration``'s ``httpMaximumConnectionsPerHost`` to ``1`` and try this again
- Switch the phone to airplane mode and try making requests

**Findings:**

Whenever we switched from WiFi to cellular (or vice versa), the next request would consistently fail
with ``-1001 kCFURLErrorTimedOut``. At the same time, we'd see the connection terminate on the server,
and we'd see logs from both Envoy and CFNetwork indicating that a new connection was established.

When we executed the next request, it would complete successfully.

If we did this more quickly and sent several requests in rapid succession,
**the first would still fail and the subsequent requests would complete normally**.

Setting the ``URLSessionConfiguration``'s ``httpMaximumConnectionsPerHost`` to ``1``
(preventing concurrent connections) and sending several requests in rapid succession resulted in all
of them failing after the ``timeoutIntervalForRequest`` specified on the ``URLSessionConfiguration``.
This is the same behavior seen with libraries like gRPC which use BSD sockets.

Putting the phone in airplane mode resulted in all requests failing immediately
(instead of waiting for the specified timeout) because iOS was aware that it had no connectivity.

Android
-------

Throughout all of the following steps, we tested to make validate if network requests succeeded
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

Reproducing the Envoy example app:

1. Build the library using ``bazel build android_dist --config=android``
2. Run the app: ``bazel mobile-install //examples/kotlin/hello_world:hello_envoy_kt --fat_apk_cpu=armeabi-v7a``

Analysis
~~~~~~~~

iOS
---

Envoy is currently configured as such:

``[URLSession] --> [Socket] --> [Envoy Mobile] --> [Socket] --> [Internet]``

With the current configuration of sending traffic over URLSession and having Envoy proxy it through,
we identified issues with Envoy being able to reconnect or switch between connections when the device
underwent various network changes such as toggling between WiFi and cellular.

The experiment above indicates that when a working connection changes to inactive (i.e., by disabling WiFi and
forcing the phone to switch to cellular), the sockets aren't notified of the change.
This is a commonly understood issue with BSD sockets on iOS, and is why Apple strongly advises against using them.

Switching networks then executing a request through URLSession resulted in the request timing out.
Executing another network request resulted in the following,
which could make it seem like Envoy was working properly at first glance (even though it wasn't):

- iOS realized that the connection was dead and terminated its socket connection with Envoy, then re-established it
- When the connection with Envoy was terminated, Envoy in turn terminated its socket connection with the outside Internet
- When iOS reconnected to Envoy, Envoy also reconnected and selected the first available connection (cellular in this case)
- Future requests succeeded because they were sent over the new/valid connection

Essentially, URLSession forced Envoy to reconnect/switch to a valid connection when a request failed due to
the fact that it was disconnecting from Envoy and reconnecting to it.

This means:

- When Envoy is called as a library (instead of proxying URLSession over a socket), it will break because nothing will force it to reconnect to a valid connection
- Restricting URLSession's concurrent connections makes this problem immediately apparent even in today's setup because the only existing connection becomes invalid

:issue:`Issue #13 <13>` will be implementing Apple-approved network solutions for the transport layer
on iOS (such as CFNetwork/Network.framework/etc.), which will resolve these problems.

Android
-------

The initial experiment was done purely by looking at the results shown on the UI in the example application. The requests
succeed after some time. To be certain that what we observed in the high level experiment were valid, we enabled ``trace``
level logging within Envoy to ensure Envoy is getting the requests back.


Open issues regarding device conditions
---------------------------------------

For current issues with device conditions, please see issues with the
`perf/device label <https://github.com/lyft/envoy-mobile/labels/perf%2Fdevice>`_.

.. _vpn_analysis:

VPN analysis
============

Given the fact that Envoy Mobile utilizes raw BSD sockets for performing API
calls today (investigations into which have been detailed in
:issue:`#13 <13>`), we wanted to validate the behavior of the library
when working with VPNs.

In order for us to consider Envoy Mobile to be "working properly" with respect
to VPNs, it needs to:

- Send all traffic over the VPN if enabled when the library starts
- Send all new requests over the VPN if the VPN is enabled **after** the library starts
- Properly recover from dead VPN connections if the VPN is disabled after the library starts
- Mirror this behavior on both iOS and Android

-------------
Investigation
-------------

~~~~~~~~~~
Experiment
~~~~~~~~~~

The following approach was taken to experiment with VPN connections on both
iOS and Android:

1. Start a service running Envoy proxy
2. Create a mobile app that performs requests to the service using Envoy Mobile
3. In our case, we used a man-in-the-middle proxy to observe requests/responses between the two, but this could also be accomplished via logging on the service
4. Open the app running Envoy Mobile with a VPN disabled
5. Note the ``x-forwarded-for`` header (or the ``x-envoy-external-address`` header which should be the same) of requests from the client
6. Enable a VPN on the mobile device
7. Monitor the above headers for changes in IP address
8. Repeat the same for disabling the VPN

On both iOS and Android...

With the above workflow, we observed an initial IP address of ``68.7.163.XXX``.

Within a second or two of enabling the Hotspot Shield VPN, requests sent
from the client changed IP addresses to ``104.232.37.XXX`` - the location of
the VPN servers.

When launching the app with the VPN enabled, all requests were seen as
coming from the VPN IP address.

Upon disabling the VPN, some requests failed before switching IP addresses
back to the original (non-VPN). This took several seconds (noticeably longer
than switching onto the VPN when it was enabled).

---------
Deep dive
---------

**Enabling the VPN**

Our understanding of why Envoy Mobile sends traffic through VPNs the way it
does is as follows.

When the library starts up with a VPN enabled, none of its clusters have been
used yet. Upon utilizing each cluster, it selects the preferred network to use
for establishing a connection. Since the OS is routing all of these through the
VPN, Envoy Mobile immediately ends up sending all traffic over the VPN.

However, when a VPN is enabled after the library is already running, any number
of clusters in use by Envoy Mobile may already have established connections.
Based on what we've seen, the OS does not seem to aggressively terminate
non-VPN connections when a VPN is enabled, and some requests continue to be
made over these pre-existing connections.

Envoy clusters regularly rotate connections in their pools, so the existing
non-VPN connections are eventually replaced by new connections which, when
created, utilize any active VPN.

Interestingly, since Envoy Mobile typically utilizes 3 clusters (``base``,
``base_wlan``, and ``base_wwan``, depending on preferred network), it's
possible that all clusters have not established connections when the VPN
becomes enabled. If this is the case and the user switches between WiFi and
cellular at the same time the VPN becomes enabled, this could prompt a faster
connection to Envoy Mobile by initializing a new cluster and its connections.

In the experiments we ran, it was merely a matter of a second or two before
all new requests typically went through the VPN once enabled.

**Disabling the VPN**

When disabling the VPN, there was a noticeable delay where requests sent
through Envoy Mobile would fail before the library recovered and sent traffic
through the non-VPN connection as expected.

Our current understanding of this behavior is that the delay is caused by
the OS destroying the VPN connection immediately, at which point Envoy Mobile
allows several failures before tearing down its connection and estabilshing
a new one.

With trace logs enabled, we saw Envoy Mobile returning some local ``503``
errors before finally destroying its connection. An example of these logs
is available in `this gist <https://gist.github.com/rebello95/da87c5029bc465f70a63861d015ee726>`_.

The reachability findings outlined below suggest that we could potentially
mitigate this delay by observing reachability updates from the OS, and work
is being tracked in :issue:`#727 <727>`.

**Reachability**

On iOS, we added additional logging to indicate reachability state from
``SCNetworkReachability`` while enabling and disabling the VPN. During these
transitions, the network remained ``.reachable``, but oscillated between
having and not having ``.transientConnection``::

  Reachability flags: .reachable] // VPN off
  Reachability [.reachable, .transientConnection] // VPN on
  Reachability flags: [.reachable] // VPN off

This behavior suggests that we could further optimize Envoy Mobile's behavior
by switching preferred networks when we detect a change in
``.transientConnection``.

When testing enabling a VPN with ``URLSession``, ``URLSession`` seemed to
switch traffic onto the VPN slightly faster than Envoy Mobile. This can likely
be attributed to the above reasoning.

-----------
Conclusions
-----------

Based on the above investigations, Envoy Mobile handles VPN connections
properly for the most part on both iOS and Android.

There is room for improvement as outlined in :issue:`#727 <727>`,
where the library could potentially more aggressively switch connections
based on the OS notifying the library of a VPN enablement/disablement.

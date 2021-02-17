.. _config_slow_start:

Slow start mode
===============

Slow start mode is a mechanism in Envoy to progressively increase amount of traffic for newly spawned service instances.
With no slow start enabled Envoy would send a proportional amount of traffic to new instances.
This could be undesirable for services that require warm up time to serve full production load and could result in request timeouts, loss of data and deteriorated user experience.



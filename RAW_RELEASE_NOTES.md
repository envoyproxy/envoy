# Raw release notes

This file contains "raw" release notes for each release. The notes are added by developers as changes
are made to Envoy that are user impacting. When a release is cut, the releaser will use these notes
to populate the [Sphinx release notes in data-plane-api](https://github.com/envoyproxy/data-plane-api/blob/master/docs/root/intro/version_history.rst).
The idea is that this is a low friction way to add release notes along with code changes. Doing this
will make it substantially easier for the releaser to "linkify" all of the release notes in the
final version.

## 1.6.0

Added supporting the priority field in eds LocalityLbEndpoints for several types of
load balancer (RoundRobinLoadBalancer, LeastRequestLoadBalancer, RandomLoadBalancer)

This allows having LocalityLbEndpoints which will be unused until/if all higher
priority LocalityLbEndpoints are sufficiently unhealthy.

See :ref:`load balancer <arch_overview_load_balancing_priority_levels>` for details.


* Added transport socket interface to allow custom implementation of transport socket. A transport socket
  provides read and write logic with buffer encryption and decryption. The exising TLS implementation is
  refactored with the interface.

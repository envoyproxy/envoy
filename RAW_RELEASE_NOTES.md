# Raw release notes

This file contains "raw" release notes for each release. The notes are added by developers as changes
are made to Envoy that are user impacting. When a release is cut, the releaser will use these notes
to populate the [Sphinx release notes in data-plane-api](https://github.com/envoyproxy/data-plane-api/blob/master/docs/root/intro/version_history.rst).
The idea is that this is a low friction way to add release notes along with code changes. Doing this
will make it substantially easier for the releaser to "linkify" all of the release notes in the
final version.

## 1.6.0
* Added transport socket interface to allow custom implementation of transport socket. A transport socket
  provides read and write logic with buffer encryption and decryption. The exising TLS implementation is
  refactored with the interface.
* Added support for dynamic response header values (`%CLIENT_IP%` and `%PROTOCOL%`).
* Added native DogStatsD support. :ref:`DogStatsdSink <envoy_api_msg_DogStatsdSink>`
* grpc-json: Added support inline descriptor in config.
* Added support for priorities for several types of load balancer.
* Added idle timeout to TCP proxy.
* Added support for dynamic headers generated from upstream host endpoint metadata
  (`UPSTREAM_METADATA(...)`).

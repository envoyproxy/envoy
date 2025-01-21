.. _config_http_tcp_bridge_golang:

Golang Http Tcp Bridge
======================

This bridge enables an Http client to connect to a Tcp server via a Golang plugin, facilitating **Protocol Convert** from Http to any RPC protocol in Envoy.

Note that the bridge is designed for sync-data-flow between go and c, so when you create new goroutines, **DO NOT** touch the request in these goroutines, they could be background goroutines.

The Bridge allows `Golang <https://go.dev/>`_ to be run during both the request
and response flows of upstream.

Go plugins used by this bridge can be recompiled independently of Envoy.

See the `Envoy's Golang HTTP-TCP Bridge proposal
<https://github.com/envoyproxy/envoy/issues/35749>`_
for more details on the filter's implementation.

Developing a Go plugin
----------------------

Envoy's Golang HTTP-TCP Bridge plugin must implement the :repo:`HttpTcpBridge API <contrib/golang/common/go/api/filter.go>`.

Here is the introduction about the HttpTcpBridge API:

- EncodeHeaders: get http request headers and decide whether to directly send rpc frame to tcp server.
- EncodeData: get http request body and convert it to rpc frame, then send that to tcp server. and you can control whether to half-close upstream conn by Envoy.
- OnUpstreamData: aggregate and verify multi rpc frame from tcp server, then convert complete rpc frame to http body, finally construct http response with headers to http client.

.. attention::
  The Go plugin API is not yet stable, you are **strongly** recommended to use the same version of Go plugin SDK and Envoy.

When you are using a release version of Envoy, i.e. 1.26.x,
you should use ``github.com/envoyproxy/envoy v1.26.x`` in the go.mod file.

When you are not using a release, i.e. the latest main branch of Envoy,
you could use ``go get -u github.com/envoyproxy/envoy@SHA`` to get the same version of Go plugin SDK,
the SHA is the latest commit sha.

Building a Go plugin
--------------------

.. attention::
  When building a Go plugin dynamic library, you **must** use a Go version consistent
  with Envoy's version of glibc.

One way to ensure a compatible Go version is to use the Go binary provided by Envoy's bazel setup:

.. code-block:: console

   $ bazel run @go_sdk//:bin/go -- version
   ...
   go version goX.YZ linux/amd64

For example, to build the ``.so`` for a ``foo`` plugin, you might run:

.. code-block:: console

   $ bazel run @go_sdk//:bin/go build -- --buildmode=c-shared  -v -o path/to/output/libfoo.so path/to/src/foo

Configuration
-------------

.. tip::
   This filter should be configured with the type URL
   :ref:`type.googleapis.com/envoy.extensions.upstreams.http.tcp.golang.v3alpha.Config <envoy_v3_api_msg_extensions.upstreams.http.tcp.golang.v3alpha.Config>`.

A prebuilt Golang Http Tcp Bridge  ``my_bridge.so`` might be configured as follows:

.. literalinclude:: /_configs/go/golang-http-tcp-bridge.yaml
   :language: yaml
   :linenos:
   :lines: 34-44
   :lineno-start: 35
   :emphasize-lines: 4-8
   :caption: :download:`golang-http-tcp-bridge.yaml </_configs/go/golang-http-tcp-bridge.yaml>`

Extensible plugin configuration
-------------------------------

Envoy's Go plugins can specify and use their own configuration.

Below is a very simple example of how such a plugin might be configured in Envoy:

.. literalinclude:: /_configs/go/golang-http-tcp-bridge-with-config.yaml
   :language: yaml
   :linenos:
   :lines: 34-48
   :lineno-start: 35
   :emphasize-lines: 4-12
   :caption: :download:`golang-http-tcp-bridge-with-config.yaml </_configs/go/golang-http-tcp-bridge-with-config.yaml>`

See the :repo:`HttpTcpBridge API <contrib/golang/common/go/api/filter.go>`
for more information about how the plugin's configuration data can be accessed.

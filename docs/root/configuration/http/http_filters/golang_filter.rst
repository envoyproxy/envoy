.. _config_http_filters_golang:

Golang
======

The HTTP Golang filter allows `Golang <https://go.dev/>`_ to be run during both the request
and response flows and makes it easier to extend Envoy.

Go plugins used by this filter can be recompiled independently of Envoy.

See the `Envoy's Golang extension proposal documentation
<https://docs.google.com/document/d/1noApyS0IfmOGmEOHdWk2-BOp0V37zgXMM4MdByr1lQk/edit?usp=sharing>`_
for more details on the filter's implementation.

Developing a Go plugin
----------------------

Envoy's Go plugins must implement the :repo:`StreamFilter API <contrib/golang/common/go/api/filter.go>`.

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
   :ref:`type.googleapis.com/envoy.extensions.filters.http.golang.v3alpha.Config <envoy_v3_api_msg_extensions.filters.http.golang.v3alpha.Config>`.

A prebuilt Golang HTTP filter  ``my_plugin.so`` might be configured as follows:

.. literalinclude:: /_configs/go/golang.yaml
   :language: yaml
   :linenos:
   :lines: 16-23
   :lineno-start: 16
   :emphasize-lines: 2-7
   :caption: :download:`golang.yaml </_configs/go/golang.yaml>`

An :ref:`HttpConnectionManager
<envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager>` can
have multiple Go plugins in its :ref:`http_filters
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.http_filters>`:

.. literalinclude:: /_configs/go/golang.yaml
   :language: yaml
   :linenos:
   :lines: 16-33
   :lineno-start: 16
   :emphasize-lines: 2-7, 9-14
   :caption: :download:`golang.yaml </_configs/go/golang.yaml>`

This can be useful if, for example, you have one plugin that provides authentication, and another
that provides connection limiting.

Extensible plugin configuration
-------------------------------

Envoy's Go plugins can specify and use their own configuration.

Below is a very simple example of how such a plugin might be configured in Envoy:

.. literalinclude:: /_configs/go/golang-with-config.yaml
   :language: yaml
   :linenos:
   :lines: 17-27
   :lineno-start: 17
   :emphasize-lines: 7-10
   :caption: :download:`golang-with-config.yaml </_configs/go/golang-with-config.yaml>`

See the :repo:`StreamFilter API <contrib/golang/common/go/api/filter.go>`
for more information about how the plugin's configuration data can be accessed.

Per-route plugin configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Go plugins can be configured on a
:ref:`per-route <envoy_v3_api_field_config.route.v3.Route.typed_per_filter_config>` basis, as in the example below:

.. literalinclude:: /_configs/go/golang-with-per-route-config.yaml
   :language: yaml
   :linenos:
   :lines: 16-44
   :lineno-start: 16
   :emphasize-lines: 2-7, 21-29
   :caption: :download:`golang-with-per-route-config.yaml </_configs/go/golang-with-per-route-config.yaml>`

Per-virtualhost plugin configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Go plugins can also be configured on a
:ref:`per-virtualhost <envoy_v3_api_field_config.route.v3.VirtualHost.typed_per_filter_config>`  basis:

.. literalinclude:: /_configs/go/golang-with-per-virtualhost-config.yaml
   :language: yaml
   :linenos:
   :lines: 16-44
   :emphasize-lines: 2-7, 21-29
   :caption: :download:`golang-with-per-virtualhost-config.yaml </_configs/go/golang-with-per-virtualhost-config.yaml>`

Complete example
----------------

Learn more about building and running a plugin for the Envoy Go filter in the step by step :ref:`Envoy Go Sandbox <install_sandboxes_golang_http>`.

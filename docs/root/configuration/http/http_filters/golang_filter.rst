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

Envoy's Go plugins must implement the :repo:`StreamFilter API <contrib/golang/filters/http/source/go/pkg/api.StreamFilter>`.

Building a Go plugin
~~~~~~~~~~~~~~~~~~~~

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

   $ bazel run @go_sdk//:bin/go build --buildmode=c-shared  -v -o path/to/output/libfoo.so path/to/src/foo

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

Plugin configuration merge policy
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Where multiple configurations are set for a plugin, the configuration is merged according
to the :ref:`merge_policy <envoy_v3_api_field_extensions.filters.http.golang.v3alpha.Config.merge_policy>`:

.. literalinclude:: /_configs/go/golang-with-merged-config.yaml
   :language: yaml
   :linenos:
   :lines: 16-58
   :emphasize-lines: 8-12, 27-34, 36-43
   :caption: :download:`golang-with-merged-config.yaml </_configs/go/golang-with-merged-config.yaml>`

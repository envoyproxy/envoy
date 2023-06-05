.. _config_network_filters_golang:

Golang
======

The Golang network filter allows `Golang <https://go.dev/>`_ to be run during both the downstream
and upstream tcp data flows and makes it easier to extend Envoy.

Go plugins used by this filter can be recompiled independently of Envoy.

See the `Envoy's Golang extension proposal documentation
<https://docs.google.com/document/d/1noApyS0IfmOGmEOHdWk2-BOp0V37zgXMM4MdByr1lQk/edit?usp=sharing>`_
for more details on the filter's implementation.

.. warning::
  The Envoy Golang filter is designed to be run with the ``GODEBUG=cgocheck=0`` environment variable set.

  This disables the cgo pointer check.

  Failure to set this environment variable will cause Envoy to crash!

Developing a Go plugin
----------------------

Envoy's Go plugins must implement the :repo:`DownstreamFilter/UpstreamFilter API <contrib/golang/common/go/api/filter.go>`.

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
   :ref:`type.googleapis.com/envoy.extensions.filters.http.golang.v3alpha.Config <envoy_v3_api_msg_extensions.filters.network.golang.v3alpha.Config>`.

A prebuilt Golang network filter  ``my_plugin.so`` might be configured as follows:

.. literalinclude:: /_configs/go/network.yaml
   :language: yaml
   :linenos:
   :lines: 11-21
   :lineno-start: 10
   :emphasize-lines: 2-11
   :caption: :download:`golang.yaml </_configs/go/network.yaml>`

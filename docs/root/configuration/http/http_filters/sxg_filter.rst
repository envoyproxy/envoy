
.. _config_http_filters_sxg:

SXG
===

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.sxg.v3alpha.SXG``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.sxg.v3alpha.SXG>`

.. attention::

  The SXG filter is experimental and is currently under active development.

This filter generates a Signed HTTP Exchange (SXG) package from a downstream web application. It uses `libsxg <https://github.com/google/libsxg/>`_ to perform the SXG packaging and signing, setting the ``Content-Type`` header to ``application/signed-exchange;v=b3`` and response body with the generated SXG document.

The SXG filter is only included in :ref:`contrib images <install_contrib>`

Transaction flow:

* check accept request header for whether client can accept SXG and set a flag. ``x-envoy-client-can-accept-sxg`` (or the header defined in ``client_can_accept_sxg_header``) will be set on the request
* If ``x-envoy-should-encode-sxg`` (or the header defined in ``should_encode_sxg_header``) is present in the response headers set a flag
* If both flags are set, buffer response body until stream end and then replace response body with generated the SXG

If there is an error generating the SXG package we fall back to the original HTML.

For more information on Signed HTTP Exchanges see `this doc <https://developers.google.com/web/updates/2018/11/signed-exchanges>`_.

.. note::

 These features are only supported on Linux amd64.

Example configuration
---------------------

The following is an example configuring the filter.

.. validated-code-block:: yaml
  :type-name: envoy.extensions.filters.http.sxg.v3alpha.SXG

  cbor_url: "/.sxg/cert.cbor"
  validity_url: "/.sxg/validity.msg"
  certificate:
    name: certificate
    sds_config:
      path: "/etc/envoy/sxg-certificate.yaml"
  private_key:
    name: private_key
    sds_config:
      path: "/etc/envoy/sxg-private-key.yaml"
  duration: 432000s
  mi_record_size: 1024
  client_can_accept_sxg_header: "x-custom-accept-sxg"
  should_encode_sxg_header: "x-custom-should-encode"
  header_prefix_filters:
    - "x-foo-"
    - "x-bar-"

Notes
-----

Instructions for generating a self-signed certificate and private key for testing can be found `here <https://github.com/WICG/webpackage/tree/master/go/signedexchange#creating-our-first-signed-exchange>`__

Statistics
----------

The SXG filter outputs statistics in the *<stat_prefix>.sxg.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  total_client_can_accept_sxg, Counter, Total requests where client passes valid Accept header for SXG documents.
  total_should_sign, Counter, Total requests where downstream passes back header indicating Envoy should encocde document.
  total_exceeded_max_payload_size, Counter, Total requests where response from downstream is to large.
  total_signed_attempts, Counter, Total requests where SXG encoding is attempted.
  total_signed_succeeded, Counter, Total requests where SXG encoding succeeds.
  total_signed_failed, Counter, Total requests where SXG encoding fails.


.. _config_http_filters_language:

Language
========

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.language.v3alpha.Language``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.language.v3alpha.Language>`

.. attention::

   The language detection filter is only included in :ref:`contrib images <install_contrib>`

.. attention::

   The language detection filter is experimental and is currently under active development.

.. attention::

   The language detection filter does not work on Windows (the blocker is getting icu compiled).

The language detection filter (i18n) picks the best match between the desired locales of a client and an application's supported locales and
adds a new ``x-language`` header to the request containing an IETF BCP 47 language tag.

The filter parses a list of locales from an ``Accept-Language`` header `RFC 2616 Section 14.4 <https://tools.ietf.org/html/rfc2616#section-14.4>`_
to match the desired locale of a client.

`Unicode ICU <https://github.com/unicode-org>`_ is used for ``Accept-Language`` header parsing.

.. code-block:: yaml

  supported_languages: [en, fr]

.. code-block:: yaml

  # Multiple types, weighted with the quality value syntax:
  Accept-Language: fr-CH, fr;q=0.9, en;q=0.8, *;q=0.5

* The client is from the Romandy region and prefers Swiss French, the variety of French spoken in the French-speaking area of Switzerland
* The client sends an ``Accept-Language`` header
* An ``Accept-Language`` header indicates the natural language and locale that the client prefers
* The application is only configured to match French fr
* The filter sets the value of the ``x-language`` header to fr

.. code-block:: yaml

  x-language: fr

When the filter can not match the desired locale of a client using the :ref:`supported_languages <envoy_v3_api_field_extensions.filters.http.language.v3alpha.Language.supported_languages>` option,
the :ref:`default_language <envoy_v3_api_field_extensions.filters.http.language.v3alpha.Language.default_language>` option will be used as a fallback. The ``x-language`` header value will never be empty.

Example configuration
---------------------

Full filter configuration:

.. code-block:: yaml

  name: envoy.filters.http.language
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.language.v3alpha.Language
    default_language: en
    supported_languages: [en, en-uk, de, dk, es, fr, zh, zh-tw]

The above configuration can be understood as follows:

* Try to pick the client's desired locale from an ``Accept-Language`` header
* At any point the filter uses `Unicode ICU <https://github.com/unicode-org>`_ for locale parsing
* If the client's desired locale can not be picked, for example because the client provided an invalid value, the :ref:`default_language <envoy_v3_api_field_extensions.filters.http.language.v3alpha.Language.default_language>` option will be used as a fallback

Statistics
----------

The language detection filter outputs statistics in the ``http.<stat_prefix>.language.`` namespace. The :ref:`stat prefix <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  header, Counter, Number of requests for which the language from the Accept-Language header (`RFC 2616 Section 14.4 <https://tools.ietf.org/html/rfc2616#section-14.4>`_) was matched
  default_language, Counter, Number of requests for which the default language was used (fallback)

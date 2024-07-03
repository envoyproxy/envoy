.. _config_http_filters_credential_injector:

Credential injector
===================

The credential injector HTTP filter serves the purpose of injecting credentials into outgoing HTTP requests.

Notice: This filter is intended to be used for workload authentication, which means that the identity associated
with the inserted credential is considered as the identity of the workload behind the Envoy proxy (in this case,
Envoy is typically deployed as a sidecar alongside that workload).

.. note::
  This filter does not handle end user authentication.

  The purpose of the filter is solely to authenticate the workload itself.

Configuration
-------------

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.credential_injector.v3.CredentialInjector``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.credential_injector.v3.CredentialInjector>`

The filter is configured with one of the following supported ``credential_injector`` extensions. Extensions are responsible for fetching the credentials
from the source. The credentials obtained are then injected into the ``Authorization`` header of the proxied HTTP requests, utilizing either the ``Basic``
or ``Bearer`` scheme.

Generic credential injector
---------------------------
* This extension should be configured with the type URL ``type.googleapis.com/envoy.extensions.http.injected_credentials.generic.v3.Generic``.
* :ref:`generic <envoy_v3_api_msg_extensions.http.injected_credentials.generic.v3.Generic>`

Here is an example configuration with Generic credential, which injects an HTTP Basic Auth credential into the proxied requests.

.. literalinclude:: _include/credential-injector-generic-filter.yaml
    :language: yaml
    :lines: 29-39
    :linenos:
    :lineno-start: 29
    :caption: :download:`credential-injector-filter.yaml <_include/credential-injector-generic-filter.yaml>`


Credential which is being used to inject a ``Basic Auth`` credential into the proxied requests:

.. literalinclude:: _include/credential-injector-generic-filter.yaml
    :language: yaml
    :lines: 57-60
    :linenos:
    :lineno-start: 57
    :caption: :download:`credential-injector-filter.yaml <_include/credential-injector-generic-filter.yaml>`

It can also be configured to inject a ``Bearer`` token into the proxied requests.

Credential for ``Bearer`` token:

.. literalinclude:: _include/credential-injector-generic-filter.yaml
    :language: yaml
    :lines: 61-64
    :linenos:
    :lineno-start: 61
    :caption: :download:`credential-injector-filter.yaml <_include/credential-injector-generic-filter.yaml>`

OAuth2 credential injector (client credential grant)
----------------------------------------------------
* This extension should be configured with the type URL ``type.googleapis.com/envoy.extensions.http.injected_credentials.oauth2.v3.OAuth2``.
* :ref:`oauth2 client credentials grant <envoy_v3_api_msg_extensions.http.injected_credentials.oauth2.v3.OAuth2>`

Here is an example configuration with OAuth2 client credential injector, which injects an OAuth2 token into the proxied requests.

.. literalinclude:: _include/credential-injector-oauth2-filter.yaml
    :language: yaml
    :lines: 25-39
    :linenos:
    :lineno-start: 25
    :caption: :download:`credential-injector-filter.yaml <_include/credential-injector-oauth2-filter.yaml>`

Statistics
----------

The HTTP credential injector filter outputs statistics in the ``http.<stat_prefix>.credential_injector.`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  ``injected``, Counter, Total number of requests with injected credentials
  ``failed``, Counter, Total number of requests that failed to inject credentials
  ``already_exists``, Counter, Total number of requests that already had credentials and overwrite is false

OAuth2 client credential injector extension specific statistics are also emitted in the ``http.<stat_prefix>.credential_injector.oauth2.`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  ``token_requested``, Counter, Total number of token requests sent to the OAuth2 server
  ``token_fetched``, Counter, Total number of successful token fetches from the OAuth2 server
  ``token_fetch_failed_on_client_secret``, Counter, Total number of times token request not sent due to missing client secret
  ``token_fetch_failed_on_cluster_not_found``, Counter, Total number of times token request not sent due to missing OAuth2 server cluster
  ``token_fetch_failed_on_bad_response_code``, Counter, Total number of times OAuth2 server responded with non-200 response code
  ``token_fetch_failed_on_bad_token``, Counter, Total number of times OAuth2 server responded with bad token
  ``token_fetch_failed_on_stream_reset``, Counter, Total number of times http stream with OAuth2 server got reset


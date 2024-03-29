.. _config_http_filters_credential_injector:

Credential Injector
===================

The credential injector HTTP filter serves the purpose of injecting credentials into outgoing HTTP requests.

The filter configuration is used to retrieve the credentials, or they can be fetched from a remote source
such as an OAuth2 authorization server. The credentials obtained are then injected into the ``Authorization``
header of the proxied HTTP requests, utilizing either the ``Basic`` or ``Bearer`` scheme.

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

Currently the filter supports :ref:`generic <envoy_v3_api_msg_extensions.http.injected_credentials.generic.v3.Generic>` only.
Other credential types can be supported as extensions.

Here is an example configuration with Generic credential, which injects an HTTP Basic Auth credential into the proxied requests.

.. literalinclude:: _include/credential-injector-filter.yaml
    :language: yaml
    :lines: 28-41
    :caption: :download:`credential-injector-filter.yaml <_include/credential-injector-filter.yaml>`


credential.yaml for Basic Auth:

.. code-block:: yaml

  resources:
  - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
    name: credential
    generic_secret:
      secret:
        inline_string: "Basic base64EncodedUsernamePassword"

It can also be configured to inject a Bearer token into the proxied requests.

credential.yaml for Bearer Token:

.. code-block:: yaml

  resources:
  - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
    name: credential
    generic_secret:
      secret:
        inline_string: "Bearer myToken"

Statistics
----------

The HTTP credential injector filter outputs statistics in the ``http.<stat_prefix>.credential_injector.`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  ``injected``, Counter, Total number of requests with injected credentials
  ``failed``, Counter, Total number of requests that failed to inject credentials
  ``already_exists``, Counter, Total number of requests that already had credentials and overwrite is false

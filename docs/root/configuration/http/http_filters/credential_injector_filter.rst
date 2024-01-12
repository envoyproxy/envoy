.. _config_http_filters_credential_injector:

Credential Injector
===================

The credential injector HTTP filter serves the purpose of injecting credentials into outgoing HTTP requests.
At present, the filter configuration is used to retrieve the credentials, or they can be requested through
the OAuth2 client credential grant. Furthermore, it has the potential to support additional credential providers
in the future, such as the OAuth2 resource owner password credentials grant, STS, etc. The credentials obtained
are then injected into the ``Authorization`` header of the proxied HTTP requests, utilizing either the ``Basic`` or ``Bearer`` scheme.

Notice: This filter is intended to be used for workload authentication, which means that the identity associated
with the inserted credential is considered as the identity of the workload behind the envoy proxy(in this case,
envoy is typically deployed as a sidecar alongside that workload). Please note that this filter does not handle
end user authentication. Its purpose is solely to authenticate the workload itself.

Configuration
-------------

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.credential_injector.v3.CredentialInjector``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.credential_injector.v3.CredentialInjector>`

Currently the filter supports :ref:`generic <envoy_v3_api_msg_extensions.injected_credentials.generic.v3.Generic>` only.
Other credential types can be supported as extensions.

Here is an example configuration with Generic credential, which injects an HTTP Basic Auth credential into the proxied requests.

.. code-block:: yaml

  overwrite: true

  credential:
    name: generic_credential
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.injected_credentials.generic.v3.Generic
      credential:
        name: credential
        sds_config:
          path_config_source:
            path: credential.yaml
      header: Authorization

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

  injected, Counter, Total number of requests with injected credentials
  failed, Counter, Total number of requests that failed to inject credentials
  already_exists, Counter, Total number of requests that already had credentials and overwrite is false

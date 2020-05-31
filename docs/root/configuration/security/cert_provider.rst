.. _config_certificate_provider:

Certificate Provider
====================

TLS certificates, the secrets, can also be fetched remotely via a Certificate Provider extension as an alternative to the secret discovery service (SDS). The Certificate Provider extension has the advantage that it does not depend on an SDS server for management of secrets (TLS certificates) but rather is responsible for managing the secrets locally. The extension typically communicates with a Certification Authority (CA) to fetch signed certificates and store them locally that are then used by cluster and listener configurations. Eliminating the dependency on the SDS server is especially important for xDS clients such as gRPC that provide a proxyless and agentless alternative to proxies such as Envoy. Another advantage of the Certificate Provider extension approach is that a secret (especially the private key part) is never shared with another entity (such as the SDS server) which is not possible in the SDS approach.

The use of Certificate Provider requires implementation of a Certificate Provider extension architecture in the xDS client as described in the document `Agentless Transport Security <https://docs.google.com/document/d/1A1_QVCrfwgkFY2YxNqbVe_eKt54kkJ6-iQq5EA_QaNc/>`_. With this implementation in place, a Certificate Provider extension can be instantiated based on the configuration provided by xDS. The gRPC project currently has plans to implement this extension architecture to take advantage of this configuration. Although Envoy currently doesn't have this implementation, it will take advantage of this configuration once the implementation is in place.


CertificateProvider Configuration
---------------------------------

:ref:`CertificateProvider <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.CommonTlsContext.certificateprovider>` is used to specify the configuration for a Certificate Provider. The field *name* is an opaque string used to specify certificate instances or types. For example, “ROOTCA” to specify a root-certificate (validation context) or “TLS” to specify a new tls-certificate. The field *typed_config* is a provider specific :ref:`configuration <envoy_v3_api_msg_config.core.v3.extension.typedextensionconfig>` where the field *name* is the name of the extension (aka plugin) to be used.

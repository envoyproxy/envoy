.. _arch_overview_ssl:

TLS
===

当和上游集群连接的时候，Envoy 在监听器中同时支持 :ref:`TLS 终结 <envoy_v3_api_field_config.listener.v3.FilterChain.transport_socket>` 和 :ref:`TLS 发起 <envoy_v3_api_field_config.cluster.v3.Cluster.transport_socket>` 。这种有效的支持确保了 Envoy 能够在现代 web 服务中发挥标准边缘代理的职责，以及和那些有高级 TLS 需求（TLS1.2、SNI 等等）的外部服务发起连接。Envoy 支持以下的 TLS 特性：

* **可配置的加密** ：每一个 TLS 监听器和客户端可以指定那些他们支持的加密算法。
* **客户端证书** ：除了服务器证书验证外，上游/客户端连接还能够提供客户端证书。
* **证书验证和固定** ：证书验证选项包括基本的证书链验证、持有者名称验证以及哈希固定。
* **证书撤销** ：如果 :ref:`提供 <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.crl>` 了证书撤销列表（CRL）， Envoy 可以根据它来检查对端证书。
* **ALPN** ：TLS 监听器支持 ALPN。HTTP 连接管理器使用这种信息（除了协议接口外）来决定一个客户端在用 HTTP/1.1 还是 HTTP/1.2。
* **SNI** ：SNI 同时支持服务器连接（监听器）和客户端连接（上游）。
* **会话（session）恢复** ：服务器连接支持通过 TLS 会话凭证来恢复之前的会话（查看 `RFC 5077 <https://www.ietf.org/rfc/rfc5077.txt>`_ ）。可以在热重启时和并行 Envoy 的实例之间执行会话恢复（在前段代理配置中是非常有用的）。
* **BoringSSL 私钥方法** ：TLS 私钥操作（签名和解密）可以通过 :ref:`扩展 <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.PrivateKeyProvider>` 来异步执行。这允许通过扩展 Envoy 来支持多个 key 的管理方案（比如 TPM）和 TLS 加速。这种机制使用了 `BoringSSL 私钥方法接口 <https://github.com/google/boringssl/blob/c0b4c72b6d4c6f4828a373ec454bd646390017d4/include/openssl/ssl.h#L1169>`_ 。
* **OCSP Stapling** ： 在线证书绑定协议（Online Certificate Stapling Protocol）响应可能要被绑定到证书中。

底层实现
-------------------------


目前 Envoy 使用 `BoringSSL <https://boringssl.googlesource.com/boringssl>`_ 作为 TLS 提供者。

.. _arch_overview_ssl_fips:

FIPS 140-2
----------

BoringSSL 可以在 `FIPS 合规模式 <https://boringssl.googlesource.com/boringssl/+/master/crypto/fipsmodule/FIPS.md>`_ 创建，使用 ``--define boringssl=fips`` Bazel 选项，根据 `BoringCrypto 模块安全政策 <https://csrc.nist.gov/CSRC/media/projects/cryptographic-module-validation-program/documents/security-policies/140sp3678.pdf>`_ 的指导来创建。目前，这个选项只在 Linux-x86_64 上有效。

FIPS 创建的正确性可以通过检查 `--version` 选项的输出中是否存在 ``BoringSSL-FIPS`` 字样来验证。


需要特别注意，对于 TIPS 合规来讲使用 FIPS-compliant 模式是非常必要的，但仅仅靠这个是远远不不够的，还需要依赖于上下文，或者还需要一些其他的步骤。其他的需求可能包含只使用经过批准的算法和/或者仅仅使用在 FIPS 合规模式下操作的模块所产生的私钥。关于更多内容，可以参考 `BoringCrypto 模块安全政策 <https://csrc.nist.gov/CSRC/media/projects/cryptographic-module-validation-program/documents/security-policies/140sp3678.pdf>`_ 和/或者 `认证的 CMVP 实验室 <https://csrc.nist.gov/projects/testing-laboratories>`_ 。


请注意 FIPS 合规构建是基于 BoringSSL 的老版本，而不是非 FIPS 构建，而且它并不支持最新的 QUIC API。

.. _arch_overview_ssl_enabling_verification:

开启证书验证
---------------------------------

上下游连接的证书验证是没开启的，除非验证信息指定了一个或者多个受信任的证书签发机构。

配置示例
^^^^^^^^^^^^^^^^^^^^^

.. literalinclude:: _include/ssl.yaml
    :language: yaml

在 Debian 系统上 */etc/ssl/certs/ca-certificates.crt* 是系统 CA 包文件的的默认路径。:ref:`trusted_ca <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.trusted_ca>` 和 :ref:`match_subject_alt_names <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.match_subject_alt_names>` 能够使 Envoy 在验证 *127.0.0.2:1234* 的服务器标识为 "foo" 时，使用与标准安装在 Debian 系统上的 cURL 用相同的方式。在 Linux 和 BSD 系统上的系统 CA 包的常见路径如下：

* /etc/ssl/certs/ca-certificates.crt (Debian/Ubuntu/Gentoo etc.)
* /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem (CentOS/RHEL 7)
* /etc/pki/tls/certs/ca-bundle.crt (Fedora/RHEL 6)
* /etc/ssl/ca-bundle.pem (OpenSUSE)
* /usr/local/etc/ssl/cert.pem (FreeBSD)
* /etc/ssl/cert.pem (OpenBSD)

其他 TLS 选项可参考 :ref:`UpstreamTlsContexts <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.UpstreamTlsContext>` 和 :ref:`DownstreamTlsContexts <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.DownstreamTlsContext>` 。

.. attention::

  If only :ref:`trusted_ca <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.trusted_ca>` is
  specified, Envoy will verify the certificate chain of the presented certificate, but not its
  subject name, hash, etc. Other validation context configuration is typically required depending
  on the deployment.

.. _arch_overview_ssl_cert_select:

证书选择
---------------------

:ref:`DownstreamTlsContexts <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.DownstreamTlsContext>` 支持多个 TLS 证书。这些可能是 RSA 和 P-256 ECDSA 证书的一种混合。以下规则适用：

* 只有特定类型（RSA 或者ECDSA）中的一个证书被指定。
* Non-P-256 服务器 ECDSA 证书被拒绝。
* 如果客户端支持 P-256 ECDSA，且在 :ref:`DownstreamTlsContext <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.DownstreamTlsContext>` 存在 P-256 ECDSA 证书，那么这个证书就会被选择，这和 OCSP 策略是一致的。
* 如果客户端只支持 RSA 证书，且在 :ref:`DownstreamTlsContext <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.DownstreamTlsContext>` 存在 RSA 证书，那么这个证书就会被选择。
* 否则，列取的第一个证书将会被使用。如果客户端只支持 RAS 证书而服务器端只支持 ECDSA 证书的话，这将会导致握手失败。
* 静态和 SDS 证书可能不会被混何在 :ref:`DownstreamTlsContext
  <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.DownstreamTlsContext>` 中。
* 被选择的证书必须符合 OCSP 策略。如果没找到符合条件的证书，连接会被拒绝。

目前只提供单个 TLS 证书 :ref:`UpstreamTlsContexts
<envoy_v3_api_msg_extensions.transport_sockets.tls.v3.UpstreamTlsContext>` 。

Secret discovery service (SDS)
安全发现服务（SDS）
------------------------------

TLS 证书可以在静态资源中指定或者从远端获取。静态资源的证书轮换是支持的，可以通过 :ref:`SDS 文件系统配置 <xds_certificate_rotation>` 或者从 SDS 服务器端获取更新信息来实现。详细信息请看 :ref:`SDS <config_secret_discovery_service>` 。

.. _arch_overview_ssl_ocsp_stapling:

OCSP Stapling
-------------

 :ref:`DownstreamTlsContexts <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.DownstreamTlsContext>` 支持在握手阶段把在线证书状态协议（OCSP）响应绑定到 TLS 证书上。 ``ocsp_staple`` 字段允许操作者在上下文中为每一个证书提供一个预先估算的 OCSP 响应。单个响应可能与多个证书无关。如果提供了响应，则 OCSP 响应必须是有效且需要检验证书没有被撤销。过期的 OCSP 响应也可被接受，但是可能会引起下游连接错误，这取决于 OCSP 的绑定策略。


:ref:`DownstreamTlsContexts <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.DownstreamTlsContext>` 支持使用 ``ocsp_staple_policy`` 字段来控制，当与证书相关联的 OCSP 响应丢失或者过期时，Envoy 是否应该停止使用证书还是继续无绑定运行。标记为 `must-staple <https://tools.ietf.org/html/rfc7633>`_ 的证书需要一个有效的 OCSP 响应，而这与 OCSP 的绑定策略无关。是实践中，一个 must-staple 证书让 Envoy 具有和 OCSP 绑定策略是 :ref:`MUST_STAPLE<envoy_v3_api_enum_value_extensions.transport_sockets.tls.v3.DownstreamTlsContext.OcspStaplePolicy.MUST_STAPLE>` 时一样的行为。在 OCSP 响应过期以后， Envoy 将不会在新连接中使用一个 must-staple 证书。

OCSP 响应将从来不会被绑定到那些通过 ``status_request`` 扩展来实现 OCSP 绑定的 TLS 请求上。

通过以下提供的运行时标志，可以调整 OCSP 的响应需求和复写 OCSP 策略。这些标志的默认值都是 ``true`` 。

* ``envoy.reloadable_features.require_ocsp_response_for_must_staple_certs`` ：禁用此标志可允许操作者在配置中忽略 must-staple 证书的 OCSP 响应。
* ``envoy.reloadable_features.check_ocsp_policy`` ：禁用此标志可以禁用 OCSP 的策略检查功能。如果客户端支持 OCSP 响应，可在 OCSP 响应可用时进行绑定，即使 OCSP 响应已过期。如果没有响应存在，绑定会被跳过。

OCSP responses are ignored for :ref:`UpstreamTlsContexts
<envoy_v3_api_msg_extensions.transport_sockets.tls.v3.UpstreamTlsContext>`.

.. _arch_overview_ssl_auth_filter:

认证过滤器
---------------------

Envoy 提供了一个网络过滤器，这个过滤器通过从 REST VPN 服务获取的信息中来匹配当前客户端证书的哈希值，以此来决定连接是否被允许。可选项 IP 允许列表也是可以被配置的。在 web 基础设施中此功能可被用来构建边缘代理的 VPN 支持。

客户端 TLS 认证过滤器 :ref:`configuration reference
<config_network_filters_client_ssl_auth>` 。

.. _arch_overview_ssl_custom_handshaker:

定制的握手器（handshaker）扩展
-------------------------------

 :ref:`CommonTlsContext <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CommonTlsContext.custom_handshaker>` 有一个 ``custom_handshaker`` 扩展，可以被用来完全重写 SSL 握手行为。这对那些很难通过回调来表达 TLS 行为的实现是非常有用的。写一个使用私钥方法的定制化握手器其实是不必要的，可以查看上面描述的 :ref:`私钥方法接口<arch_overview_ssl>` 。

为了避免重复实现所有的 `Ssl::ConnectionInfo <https://github.com/envoyproxy/envoy/blob/64bd6311bcc8f5b18ce44997ae22ff07ecccfe04/include/envoy/ssl/connection.h#L19>`_ 接口，可以选择一个定制化的实现来扩展 `Envoy::Extensions::TransportSockets::Tls::SslHandshakerImpl <https://github.com/envoyproxy/envoy/blob/64bd6311bcc8f5b18ce44997ae22ff07ecccfe04/source/extensions/transport_sockets/tls/ssl_handshaker.h#L40>`_ 。

定制化的握手器需要通过 `握手器能力 <https://github.com/envoyproxy/envoy/blob/64bd6311bcc8f5b18ce44997ae22ff07ecccfe04/include/envoy/ssl/handshaker.h#L68-L89>`_ 来显式声明。默认的 Envoy 握手器将会管理剩余的事情。

一个名为 ``SslHandshakerImplForTest`` 的握手器示例，可在 `这个测试 <https://github.com/envoyproxy/envoy/blob/64bd6311bcc8f5b18ce44997ae22ff07ecccfe04/test/extensions/transport_sockets/tls/handshaker_test.cc#L174-L184>`_ 中查看，且演示了特殊情况下 ``SSL_ERROR`` 的处理和回调。


.. _arch_overview_ssl_trouble_shooting:

故障排除
----------------

当和上游集群发生连接的时候，Envoy 会发起 TLS，任何错误都会被记录在 :ref:`UPSTREAM_TRANSPORT_FAILURE_REASON<config_access_log_format_upstream_transport_failure_reason>` 字段或者 `AccessLogCommon.upstream_transport_failure_reason<envoy_v3_api_field_data.accesslog.v3.AccessLogCommon.upstream_transport_failure_reason>` 字段。

* ``SDS 没有提供加密信息（Secret is not supplied by SDS）`` ：Envoy 一直在等待 SDS 能够传递 key/cert 或者 root 证书。
* ``SSLV3_ALERT_CERTIFICATE_EXPIRED`` ： 对端证书过期且在配置中不被允许。
* ``SSLV3_ALERT_CERTIFICATE_UNKNOWN`` ： 对端证书不在指定的 SPKI 配置中。
* ``SSLV3_ALERT_HANDSHAKE_FAILURE`` ：握手失败，通常是由于上游需要客户端证书，但是却没有提供。
* ``TLSV1_ALERT_PROTOCOL_VERSION`` ：TLS 协议版本不匹配。
* ``TLSV1_ALERT_UNKNOWN_CA`` ：对端证书的签发机构不在受信任的签发机构名单中。


更多由 BoringSSL 引发的错误的详细列表信息可以在 `这儿 <https://github.com/google/boringssl/blob/master/crypto/err/ssl.errordata>`_ 找到。

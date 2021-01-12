.. _operations_certificates:

证书管理
==========

Envoy 提供几种证书管理机制。从高层次来讲，可以分为：

1. 静态 :ref:`CommonTlsContext <envoy_api_msg_auth.CommonTlsContext>` 引用的证书。这些证书将*不会*自动加载，需要通过重启代理或者重新加载引用它们的集群/监听器。:ref:`热重启 <arch_overview_hot_restart>` 可以被用来获取新证书而不用丢弃流量。

2. :ref:`证书发现服务 <config_secret_discovery_service>` 引用的证书。通过使用 SDS，证书可以作为文件被引用（当父目录被移除时，证书会被重新加载）或者通过一个可以推送新证书的外部 SDS 服务器。

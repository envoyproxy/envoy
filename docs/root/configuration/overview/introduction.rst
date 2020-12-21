介绍
=====

Envoy xDS API 在 :repo:`api 树 <api/>` 中被定义为 `proto3
<https://developers.google.com/protocol-buffers/docs/proto3>`_ `Protocol Buffers
<https://developers.google.com/protocol-buffers/>`_ 。它们支持：

* 使用 gRPC :ref:`xDS <xds_protocol>` API 更新的流传输。这减少了资源需求且降低了更新延时。
* 使用 `proto3
  规范 JSON 映射
  <https://developers.google.com/protocol-buffers/docs/proto3#json>`_ 衍生得到一个新的基于 JSON/YAML 格式的 REST-JSON API。
* 通过文件系统、REST-JSON 或者 gRPC 端点的更新传输。
* 通过一个扩展的端点分配 API 和负载来实现的高级负载均衡，以及向管理服务器上报资源利用率。 
* 当需要时的 :ref:`强一致性和有序性 <xds_protocol_eventual_consistency_considerations>` 。API 仍然需要保持基线最终一致性模型。

关于 Envoy 和管理服务器之间 xDS 消息交换的更进一步详细信息，可查看 :ref:`xDS 协议描述 <xds_protocol>`。

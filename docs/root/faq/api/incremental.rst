增量 xDS 支持的状态有哪些？
==============================================

 :ref:`增量 xDS <xds_protocol_delta>` 协议通过设计两种机制来提高效率、可扩展性和功能性：

* Delta xDS. 只会下发资源的变更，而不是全量下发。
* On-demand xDS. 可以根据请求内容延迟加载资源。

如今，所有的 xDS 协议（包括 ADS）都支持 delta xDS 。On-demand xDS 目前只支持
:ref:`VHDS <config_http_conn_man_vhds>`。

.. _config_access_log:

访问日志
==============

配置
-------------------------

访问日志是 :ref:`HTTP 连接管理器配置
<config_http_conn_man>` 或 :ref:`TCP 代理配置 <config_network_filters_tcp_proxy>` 配置的一部分。

* :ref:`v3 API 参考 <envoy_v3_api_msg_config.accesslog.v3.AccessLog>`

.. _config_access_log_format:

格式规则
------------

访问日志格式包含用于提取和插入有关数据的命令操作符。
他们支持两种格式: :ref:`"格式字符串" <config_access_log_format_strings>` 和
:ref:`"格式词典" <config_access_log_format_dictionaries>`。两种情况下，命令操作符
用于提取那些随后将被插入到特定日志格式中的有关数据。
一次只能指定一种访问日志格式。

.. _config_access_log_format_strings:

格式字符串
--------------

格式字符串是使用关键字 ``format`` 描述的纯字符串。他们可能包含命令操作符或者是其他被解释成纯字符串的字符
访问日志格式化器不假设新的行分隔符，因此必须将其指定为格式字符串的一部分。
例如 :ref:`默认格式 <config_access_log_default_format>`。

.. _config_access_log_default_format:

默认格式
---------------------

如果自定义格式未指定，Envoy 使用如下的默认格式:

.. code-block:: none

  [%START_TIME%] "%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%"
  %RESPONSE_CODE% %RESPONSE_FLAGS% %BYTES_RECEIVED% %BYTES_SENT% %DURATION%
  %RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)% "%REQ(X-FORWARDED-FOR)%" "%REQ(USER-AGENT)%"
  "%REQ(X-REQUEST-ID)%" "%REQ(:AUTHORITY)%" "%UPSTREAM_HOST%"\n

默认 Envoy 访问日志格式举例:

.. code-block:: none

  [2016-04-15T20:17:00.310Z] "POST /api/v1/locations HTTP/2" 204 - 154 0 226 100 "10.0.35.28"
  "nsq2http" "cc21d9b0-cf5c-432b-8c7e-98aeb7988cd2" "locations" "tcp://10.0.2.1:80"

.. _config_access_log_format_dictionaries:

格式词典
-------------------

格式词典是指定结构化访问日志输出格式的词典，通过 ``json_format`` 或者 ``typed_json_format`` 关键字指定。 
允许日志被输出到结构化的格式中（如 JSON）。和格式字符串类似，命令操作符被解析并将其值插入到用于构造日志输出的格式词典中。

如下给出了一个 ``json_format`` 配置的示例:

.. code-block:: json

  {
    "config": {
      "json_format": {
          "protocol": "%PROTOCOL%",
          "duration": "%DURATION%",
          "my_custom_header": "%REQ(MY_CUSTOM_HEADER)%"
      }
    }
  }

与此对应，下面的 JSON 对象会被写入到日志文件中：

.. code-block:: json

  {"protocol": "HTTP/1.1", "duration": "123", "my_custom_header": "value_of_MY_CUSTOM_HEADER"}

每个命令操作符允许指定一个自定义关键字。

``typed_json_format`` 不同于 ``json_format`` ，它的值会被渲染成合适的 JSON 数字、布尔和嵌套对象或列表。
在例子中，请求持续时间会被渲染成一个数字 ``123``。

格式词典有如下限制：

* 词典必须是字符串到字符串的映射（具体来说，是字符串到命令操作符）。支持嵌套。
* 当使用 ``typed_json_format`` 命令操作符时，如果命令操作符是出现在词典值域中的唯一字符串会产生确定类型输出。例如，
  ``"%DURATION%"`` 会记录成一个持续值的数值，但是 ``"%DURATION%.0"`` 会记录成一个字符串。

.. note::

  当使用 ``typed_json_format`` 时，超过 :math:`2^{53}` 的整数，由于需要转换成浮点数因此会使精度降低。

.. _config_access_log_command_operators:

命令运算符
-----------------

命令运算符用于提取那些将被插入到访问日志中值。
相同的运算符在不同类型的访问日志（例如 HTTP 和 TCP）中用处不同。 不同日志类型中有些字段可能有细微的差别。 注意不同点。

注意如果一个值未设置或为空，日志将会包含一个 ``-`` 或者 JSON 日志中的字符串 ``"-"``。
对于有类型的 JSON 日志来说，为设置的值会被表示成 ``null`` 值，并且空字符串会被渲染成 ``""`` :ref:`omit_empty_values
<envoy_v3_api_field_config.core.v3.SubstitutionFormatString.omit_empty_values>` 选项可用于完全忽略空值。

除非另有说明，命令操作符生成针对 JSON 日志的字符串输出。

支持如下命令操作符：

.. _config_access_log_format_start_time:

%START_TIME%
  HTTP
    请求开始时间（包括毫秒）。

  TCP
    下游连接开始时间（包括毫秒）。

  START_TIME 可以通过 `format string <https://en.cppreference.com/w/cpp/io/manip/put_time>`_ 自定义。
  除此之外, START_TIME 也允许如下格式符：

  +------------------------+---------------------------------------------------------------+
  | 格式符                 | 说明                                                          |
  +========================+===============================================================+
  | ``%s``                 | 从 1970-01-01 00:00:00 UTC 开始的秒数                         |
  +------------------------+---------------------------------------------------------------+
  | ``%f``, ``%[1-9]f``    | 小数秒位数，默认 9 位 (纳秒)                                  |
  |                        +---------------------------------------------------------------+
  |                        | - ``%3f`` 毫秒 (3 位)                                         |
  |                        | - ``%6f`` 微秒 (6 位)                                         |
  |                        | - ``%9f`` 纳秒 (9 位)                                         |
  +------------------------+---------------------------------------------------------------+

  如下是 START_TIME 格式的举例：

  .. code-block:: none

    %START_TIME(%Y/%m/%dT%H:%M:%S%z %s)%

    # To include millisecond fraction of the second (.000 ... .999). E.g. 1527590590.528.
    %START_TIME(%s.%3f)%

    %START_TIME(%s.%6f)%

    %START_TIME(%s.%9f)%

  在确定类型的 JSON 日志中，START_TIME 通常被渲染成字符串。

%BYTES_RECEIVED%
  HTTP
    接收到消息体字节数。

  TCP
    在连接上从下游接收的字节数。

  在确定类型的 JSON 日志中，渲染成一个数值类型。

%PROTOCOL%
  HTTP
    协议。目前不是 *HTTP/1.1* 就是 *HTTP/2*。

  TCP
    未实现 ("-")。

  在确定类型的 JSON 日志中，PROTOCOL 会被渲染成字符串。如果协议不可用(如： 在 TCP 日志中) 为 ``"-"`` 。

%RESPONSE_CODE%
  HTTP
    HTTP 响应码。注意响应码 '0' 表示服务器从未发送过响应数据。 通常认为是（下游）客户端断开。

  TCP
    未实现 ("-")。

  在确定类型的 JSON 日志中，渲染成一个数值类型。

.. _config_access_log_format_response_code_details:

%RESPONSE_CODE_DETAILS%
  HTTP
    HTTP 响应状态码详情提供关于响应状态码的附加信息， 例如谁设置了它 (上游 或者 envoy) 和原因。

  TCP
    未实现 ("-")

.. _config_access_log_format_connection_termination_details:

%CONNECTION_TERMINATION_DETAILS%
  HTTP 和 TCP
    连接中断详情会提供和连接被 Envoy 在 L4 中断原因相关的的附加信息。

%BYTES_SENT%
  HTTP
    发送的包体字节数。 对于 WebSocket 连接将会包含响应头字节数。

  TCP
    在连接上发送给下游的字节数。

  在确定类型的 JSON 日志中，渲染成一个数值类型。

%DURATION%
  HTTP
    请求从起始时间到最后一个字节发出的持续总时长（以毫秒为单位）。

  TCP
    下游连接的持续总时长（以毫秒为单位）。

  在确定类型的 JSON 日志中，渲染成一个数值类型。

%REQUEST_DURATION%
  HTTP
    请求从起始时间到接收完下游的最后一个字节为止的持续总时长（以毫秒为单位）。

  TCP
    未实现 ("-")。

  在确定类型的 JSON 日志中，渲染成一个数值类型。

%RESPONSE_DURATION%
  HTTP
    请求从起始时间直到上游的第一个字节到达为止的持续总时长（以毫秒为单位）。

  TCP
    未实现 ("-")。

  在确定类型的 JSON 日志中，渲染成一个数值类型。

%RESPONSE_TX_DURATION%
  HTTP
    请求从读第一个来自于上游主机的字节开始到给下游发完最后一个字节的持续时常（以毫秒为单位）。

  TCP
    未实现 ("-")。

  在确定类型的 JSON 日志中，渲染成一个数值类型。

.. _config_access_log_format_response_flags:

%RESPONSE_FLAGS%
  如果有的话，表示响应或者连接的附加详情。 对于 TCP 连接，说明中提到的响应码不适用。 可能的值如下：

  HTTP 和 TCP
    * **UH**: 附加在 503 响应状态码，表示上游集群中无健康的上游主机。
    * **UF**: 附加在 503 响应状态码，表示上游连接失败。
    * **UO**: 附加在 503 响应状态码，表示上游溢出 (:ref:`熔断 <arch_overview_circuit_break>`) 。
    * **NR**: 附加在 404 响应状态码，表示无给定请求的 :ref:`路由配置 <arch_overview_http_routing>` ，或者对于一个下游连接没有匹配的过滤器链。
    * **URX**: 请求因为达到了 :ref:`上游重试限制 (HTTP) <envoy_v3_api_field_config.route.v3.RetryPolicy.num_retries>`  或者 :ref:`最大连接尝试 (TCP) <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.max_connect_attempts>` 而被拒绝。
  HTTP 独有
    * **DC**: 下游连接中断。
    * **LH**: 附加在 503 响应状态码，本地服务 :ref:`健康检查请求 <arch_overview_health_checking>` 失败。
    * **UT**: 附加在 504 响应状态码，上游请求超时。
    * **LR**: 附加在 503 响应状态码，连接在本地被重置。
    * **UR**: 附加在 503 响应状态码，连接在远程被重置。
    * **UC**: 附加在 503 响应状态码，上游连接中断。
    * **DI**: 通过 :ref:`故障注入 <config_http_filters_fault_injection>` 使请求处理被延迟一个指定的时间。
    * **FI**: 通过 :ref:`故障注入 <config_http_filters_fault_injection>` 使请求被终止掉并带有一个相应码。 
    * **RL**: 附加在 429 相应状态码，请求被 :ref:`HTTP 限流过滤器 <config_http_filters_rate_limit>` 本地限流。
    * **UAEX**: 请求被外部授权服务拒绝。
    * **RLSE**: 因限流服务出现错误而拒绝请求。
    * **IH**: 附加在 400 响应状态码，请求被拒绝因为他为
      :ref:`strictly-checked header <envoy_v3_api_field_extensions.filters.http.router.v3.Router.strict_check_headers>` 设置了一个无效值。
    * **SI**: 附加在 408 相应状态码，流闲置超时。
    * **DPE**: 下游请求存在一个 HTTP 协议错误。
    * **UMSDR**: 上游请求达到最大流持续时长。

%ROUTE_NAME%
  路由名。

%UPSTREAM_HOST%
  上游主机 URL（如，TCP 连接 tcp://ip:port）。

%UPSTREAM_CLUSTER%
  上游主机所属的上游集群。

%UPSTREAM_LOCAL_ADDRESS%
  上游连接的本地地址。如果地址是 IP 地址，则会包含地址和端口。

.. _config_access_log_format_upstream_transport_failure_reason:

%UPSTREAM_TRANSPORT_FAILURE_REASON%
  HTTP
    如果上游因传输套接字（例如 TLS 握手）而连接失败，从传输套接字中提供失败原因。 这个字段的格式依赖于上游传输套接字的配置。
    公共的 TLS 失败在 :ref:`TLS 故障排除 <arch_overview_ssl_trouble_shooting>`。

  TCP
    未实现 ("-")。

%DOWNSTREAM_REMOTE_ADDRESS%
  下游连接的远程地址。如果地址是 IP 地址，它会包含地址和端口。

  .. note::

    如果地址是从 :ref:`proxy proto <envoy_v3_api_field_config.listener.v3.FilterChain.use_proxy_proto>` 或 :ref:`x-forwarded-for
    <config_http_conn_man_headers_x-forwarded-for>` 推断而来，则可能不是对端的物理远程地址。

%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%
  下游连接的远程地址。如果地址是 IP 地址，它 *不* 包含端口。

  .. note::

    如果地址是从 :ref:`proxy proto <envoy_v3_api_field_config.listener.v3.FilterChain.use_proxy_proto>` 或 :ref:`x-forwarded-for
    <config_http_conn_man_headers_x-forwarded-for>` 推断而来，则可能不是对端的物理远程地址。

%DOWNSTREAM_DIRECT_REMOTE_ADDRESS%
  下游连接的直达远程地址。如果地址是 IP 地址，它会包含地址和端口。

  .. note::

    通常是对端的物理远程地址，即使上游远程地址是从 :ref:`proxy proto <envoy_v3_api_field_config.listener.v3.FilterChain.use_proxy_proto>`
    或 :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>` 推断而来。

%DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT%
  下游连接的直达远程地址。如果地址是 IP 地址，它*不*包含端口。

  .. note::

    通常是对端的物理远程地址，即使上游远程地址是从 :ref:`proxy proto <envoy_v3_api_field_config.listener.v3.FilterChain.use_proxy_proto>`
    或 :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>` 推断而来。

%DOWNSTREAM_LOCAL_ADDRESS%
  下游连接的本地地址。如果地址是 IP 地址，它会包含地址和端口。
  如果原始连接被 iptables 的 REDIRECT 重定向，则代表通过 :ref:`原始目的过滤器 <config_listener_filters_original_dst>` 
  经过 SO_ORIGINAL_DST 套接字选项重存的原始目的地址。
  如果原始连接被 iptables 的 TPROXY 重定向，并且监听器的 transparent 选项设置为 true，则代表原始的目的地地址和端口。

%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%
    与 **%DOWNSTREAM_LOCAL_ADDRESS%** 相同，如果是IP地址不包含端口。

.. _config_access_log_format_connection_id:

%CONNECTION_ID%
  下游连接的标识。它可以用于交叉引用跨多个日志汇聚的 TCP 访问日志， 或者对同一个连接交叉引用基于时间的报告。
  标识在一个执行程序中大概率是是唯一的，但是在多个实例或者重启实例间可以重复。

%GRPC_STATUS%
  gRPC 状态码，易于用数字对应的消息进行翻译

%DOWNSTREAM_LOCAL_PORT%
  类似 **%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%**，但是只提取 **%DOWNSTREAM_LOCAL_ADDRESS%** 的端口部分。

%REQ(X?Y):Z%
  HTTP
    HTTP 请求头，其中 X 是首选的 HTTP 头，Y 是一个替代值，Z 是一个可选参数表示字符串截断至 Z 字符长。
    其值首先从名字为 X 的 HTTP 请求头获取，如果其未设置，那么使用请求头 Y。如果请求头均是 none，则在日志中表示成 '-' 符号。

  TCP
    未实现 ("-")。

%RESP(X?Y):Z%
  HTTP
    除了从 HTTP 响应头中获取以外，其他和 **%REQ(X?Y):Z%** 相同。

  TCP
    未实现 ("-")。

%TRAILER(X?Y):Z%
  HTTP
    除了从 HTTP 响应尾中获取以外，其他和 **%REQ(X?Y):Z%** 相同。

  TCP
    未实现 ("-")。

%DYNAMIC_METADATA(NAMESPACE:KEY*):Z%
  HTTP
    :ref:`动态元数据 <envoy_v3_api_msg_config.core.v3.Metadata>` 信息，
    当设置元信息时 NAMESPACE 是过滤命名空间的过滤器器，KEY 是命名空间内可选查找关键字，可以被可选嵌套关键字 ':' 切分，
    Z 是一个可选参数表示字符串截断至 Z 字符长。动态元数据可以被过滤器用:repo:`StreamInfo <include/envoy/stream_info/stream_info.h>` API:
    *setDynamicMetadata* 设置。数据会被记录成 JSON 字符串。如下动态参数：

    ``com.test.my_filter: {"test_key": "foo", "test_object": {"inner_key": "bar"}}``

    * %DYNAMIC_METADATA(com.test.my_filter)% will log: ``{"test_key": "foo", "test_object": {"inner_key": "bar"}}``
    * %DYNAMIC_METADATA(com.test.my_filter:test_key)% will log: ``"foo"``
    * %DYNAMIC_METADATA(com.test.my_filter:test_object)% will log: ``{"inner_key": "bar"}``
    * %DYNAMIC_METADATA(com.test.my_filter:test_object:inner_key)% will log: ``"bar"``
    * %DYNAMIC_METADATA(com.unknown_filter)% will log: ``-``
    * %DYNAMIC_METADATA(com.test.my_filter:unknown_key)% will log: ``-``
    * %DYNAMIC_METADATA(com.test.my_filter):25% will log (truncation at 25 characters): ``{"test_key": "foo", "test``

  TCP
    未实现 ("-")。

  .. note::

    对于确定类型的 JSON 日志，当引用的关键字是单值时，这个运算法渲染成字符串、数值或布尔类型的单值。
    如果引用的关键字是一个结构或者列表值，会被渲染成 JSON 的结构或者列表。 结构和列表可能是嵌套的，则最大长度的限制会被忽略。

.. _config_access_log_format_filter_state:

%FILTER_STATE(KEY:F):Z%
  HTTP
    :ref:`过滤器状态 <arch_overview_data_sharing_between_filters>` 信息，KEY 是查找过滤器状态对象的必选项。
    被序列化的 proto 会尽可能的被记录成 JSON 字符串。
    如果对于 Envoy 来说被序列化的 proto 是未知类型，它会被记录成 protobuf 调试字符串。
    Z 是一个可选参数，表示字符串截断至 Z 字符长。
    F 是一个可选参数，表明采用哪个 FilterState 方法，完成序列化。
    如果 'PLAIN' 设置了，过滤器状态对象会被序列化成一个非结构化字符串。
    如果 'TYPED' 设置了或者没有提供 F，过滤器状态对象会被序列化成一个 JSON 字符串。

  TCP
    和 HTTP 相同，过滤器状态来自于连接而非一个 L7 层的请求。

  .. note::

    对于确定类型的 JSON 日志，当引用的关键字是单值时，这个运算法渲染成字符串、数值或布尔类型的单值。
    如果引用的关键字是一个结构或者列表值，会被渲染成 JSON 的结构或者列表。 结构和列表可能是嵌套的，则最大长度的限制会被忽略。

%REQUESTED_SERVER_NAME%
  HTTP
    设置在 ssl 连接套接字上表示服务器名称指示 (SNI) 的字符值
  TCP
    设置在 ssl 连接套接字上表示服务器名称指示 (SNI) 的字符值

%DOWNSTREAM_LOCAL_URI_SAN%
  HTTP
    用于建立下游 TLS 连接的本地 SAN 证书中的 URI。
  TCP
    用于建立下游 TLS 连接的本地 SAN 证书中的 URI。
    
%DOWNSTREAM_PEER_URI_SAN%
  HTTP
    用于建立下游 TLS 连接的对端 SAN 证书中的 URI。
  TCP
    用于建立下游 TLS 连接的对端 SAN 证书中的 URI。

%DOWNSTREAM_LOCAL_SUBJECT%
  HTTP
    用于建立下游 TLS 连接的本地证书中的主题。
  TCP
    用于建立下游 TLS 连接的本地证书中的主题。
  
%DOWNSTREAM_PEER_SUBJECT%
  HTTP
    用于建立下游 TLS 连接的对端证书中的主题。
  TCP
    用于建立下游 TLS 连接的对端证书中的主题。

%DOWNSTREAM_PEER_ISSUER%
  HTTP
    用于建立下游 TLS 连接的对端证书的颁发者。
  TCP
    用于建立下游 TLS 连接的对端证书的颁发者。

%DOWNSTREAM_TLS_SESSION_ID%
  HTTP
    已建立的下游 TLS 连接的会话 ID。
  TCP
    已建立的下游 TLS 连接的会话 ID。

%DOWNSTREAM_TLS_CIPHER%
  HTTP
    用于建立下游 TLS 连接的加密算法集的 OpenSSL 名称。
  TCP
    用于建立下游 TLS 连接的加密算法集的 OpenSSL 名称。

%DOWNSTREAM_TLS_VERSION%
  HTTP
    用于建立下游 TLS 连接的 TLS 版本（如，``TLSv1.2``，``TLSv1.3``）。
  TCP
    用于建立下游 TLS 连接的 TLS 版本（如，``TLSv1.2``，``TLSv1.3``）。
    
%DOWNSTREAM_PEER_FINGERPRINT_256%
  HTTP
    用于建立下游 TLS 连接的客户端证书的十六进制编码 SHA256 指纹。
  TCP
    用于建立下游 TLS 连接的客户端证书的十六进制编码 SHA256 指纹。

%DOWNSTREAM_PEER_FINGERPRINT_1%
  HTTP
    用于建立下游 TLS 连接的客户端证书的十六进制编码 SHA1 指纹。
  TCP
    用于建立下游 TLS 连接的客户端证书的十六进制编码 SHA1 指纹。

%DOWNSTREAM_PEER_SERIAL%
  HTTP
    用于建立下游 TLS 连接的客户端证书的序列号。
  TCP
    用于建立下游 TLS 连接的客户端证书的序列号。

%DOWNSTREAM_PEER_CERT%
  HTTP
    基于URL-encoded PEM 格式的客户端证书，该证书用于建立下游 TLS 连接的。
  TCP
    基于URL-encoded PEM 格式的客户端证书，该证书用于建立下游 TLS 连接的。

%DOWNSTREAM_PEER_CERT_V_START%
  HTTP
    客户端证书的有效起始日期，该证书用于建立下游 TLS 连接。
  TCP
    客户端证书的有效起始日期，该证书用于建立下游 TLS 连接。

%DOWNSTREAM_PEER_CERT_V_END%
  HTTP
    客户端证书的有效结束日期，该证书用于建立下游 TLS 连接。
  TCP
    客户端证书的有效结束日期，该证书用于建立下游 TLS 连接。

%HOSTNAME%
  系统主机名。

%LOCAL_REPLY_BODY%
  被 Envoy 拒绝的请求体文本。

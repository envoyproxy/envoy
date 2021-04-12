.. _config_http_filters_lua:

Lua
===

.. attention::

  默认情况下构建出的 Envoy 不会提供以共享库方式安装 Lua 模块时需要的 symbols。但是 Envoy 可以按支持提供 symbols 的方式进行构建。请查看 :repo:`Bazel 文档 <bazel/README.md>` 获取更多信息。

概述
--------

HTTP Lua 过滤器允许在请求和响应流期间运行 `Lua <https://www.lua.org/>`_ 脚本。`LuaJIT <https://luajit.org/>`_ 作为运行时使用，因此，支持的 Lua 版本主要是 5.1，同时也包括部分 5.2 的特性。查看 `LuaJIT 文档 <https://luajit.org/extensions.html>`_ 获取更多详情。

.. note::

  `moonjit <https://github.com/moonjit/moonjit/>`_ 是 LuaJIT 开发的延续，它支持了更多的 5.2 特性和额外的结构。可以使用如下 bazel 选项构建支持 moonjit 的 Envoy： ``--//source/extensions/filters/common/lua:moonjit=1``。

过滤器和 Lua 支持的高层设计如下：

* 所有的 Lua 环境都是 :ref:`每个工作线程内的 <arch_overview_threading>`。这意味着不存在真正的全局数据。在加载时创建和填充的所有全局变量都将在每个工作线程中独立展示。将来可能通过 API 添加真正的全局支持。
* 所有的脚本都以协程方式运行。这意味着即便它们可能执行复杂的异步任务，但它们也是按照同步的方式编写。这使得脚本实际上更容易编写。Envoy 通过一组 API 执行所有的网络/异步处理。Envoy 将适当地暂停脚本地执行，并在异步任务完成后继续执行脚本。
* **不要在脚本中执行阻塞操作。** Envoy API 用于所有的 IO，所以这对性能至关重要。

当前支持的高级特性
---------------------------------------

**注意：** 随着生产中使用该过滤器，预计该列表会随着时间的推移而拓展。API 表层保持精简。目的是使脚本尽可能简单和编写安全。非常复杂或高性能的使用场景应当使用原生的 C++ 过滤器 API。

* 在流式传输的请求流和/或响应流中检查头部、正文和尾部。
* 修改头部和尾部。
* 阻塞并缓存整个请求/响应正文以进行检查。
* 对上游主机执行出站异步 HTTP 调用。可以在缓冲正文数据的同时执行此类调用，以便在调用完成时可以修改上游头部。
* 执行直接响应并跳过后续的过滤器迭代。例如，脚本可以向上游发起 HTTP 身份认证调用，然后直接响应 403 响应码。

配置
-------------

* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.lua.v3.Lua>`
* 此过滤器应使用配置名称 *envoy.filters.http.lua*。

配置仅包含 :ref:`inline_code <envoy_v3_api_field_extensions.filters.http.lua.v3.Lua.inline_code>` 的 Lua HTTP 过滤器的简单示例如下：

.. code-block:: yaml

  name: envoy.filters.http.lua
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
    inline_code: |
      -- Called on the request path.
      function envoy_on_request(request_handle)
        -- Do something.
      end
      -- Called on the response path.
      function envoy_on_response(response_handle)
        -- Do something.
      end

默认情况下，定义在 ``inline_code`` 中的 Lua 脚本会被认定为 ``GLOBAL`` 脚本。Envoy 将会在每个 HTTP 请求中执行该脚本。

基于每条路由的配置
-----------------------

通过在虚拟主机、路由或加权集群上提供 :ref:`LuaPerRoute <envoy_v3_api_msg_extensions.filters.http.lua.v3.LuaPerRoute>` 配置，还可以基于每条路由禁用或覆盖 Lua HTTP 过滤器。

LuaPerRoute 提供了两种覆盖 `GLOBAL` Lua 脚本的方式：

* 通过提供一个与已定义的 :ref:`命名 Lua 源码映射 <envoy_v3_api_field_extensions.filters.http.lua.v3.Lua.source_codes>` 相关联的名称。
* 通过提供内联 :ref:`源码 <envoy_v3_api_field_extensions.filters.http.lua.v3.LuaPerRoute.source_code>` （这允许通过 RDS 发送代码）。

给出以下 Lua 过滤器配置作为具体实例：

.. code-block:: yaml

  name: envoy.filters.http.lua
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
    inline_code: |
      function envoy_on_request(request_handle)
        -- 执行某些逻辑
      end
    source_codes:
      hello.lua:
        inline_string: |
          function envoy_on_request(request_handle)
            request_handle:logInfo("Hello World.")
          end
      bye.lua:
        inline_string: |
          function envoy_on_response(response_handle)
            response_handle:logInfo("Bye Bye.")
          end

可以通过 :ref:`LuaPerRoute <envoy_v3_api_msg_extensions.filters.http.lua.v3.LuaPerRoute>` 配置在某些虚拟主机、路由或加权集群上禁用 HTTP Lua 过滤器，如下所示：

.. code-block:: yaml

  per_filter_config:
    envoy.filters.http.lua:
      disabled: true

我们还可以通过在 LuaPerRoute 中指定名称来引用过滤器配置中的 Lua 脚本。``GLOBAL`` Lua 脚本会被引用的脚本覆盖：

.. code-block:: yaml

  per_filter_config:
    envoy.filters.http.lua:
      name: hello.lua

.. attention::

  ``GLOBAL`` 作为 :ref:`Lua.inline_code <envoy_v3_api_field_extensions.filters.http.lua.v3.Lua.inline_code>` 中的保留名称。因此，请勿使用 ``GLOBAL`` 作为其他 Lua 脚本的名称。

或者我们可以直接在 LuaPerRoute 中定义一个新的脚本，以覆盖 `GLOBAL` Lua 脚本，如下所示：

.. code-block:: yaml

  per_filter_config:
    envoy.filters.http.lua:
      source_code:
        inline_string: |
          function envoy_on_response(response_handle)
            response_handle:logInfo("Goodbye.")
          end


脚本示例
---------------

本章节提供一些具体的 Lua 脚本示例，作为更友好的介绍和快速入门。更多的 API 支持详情请参考 :ref:`流处理 API <config_http_filters_lua_stream_handle_api>`。

.. code-block:: lua

  -- 在请求路径上调用。
  function envoy_on_request(request_handle)
    -- 等待整个请求正文并添加正文大小到请求头部。
    request_handle:headers():add("request_body_size", request_handle:body():length())
  end

  -- 在响应路径上调用。
  function envoy_on_response(response_handle)
    -- 等待整个响应正文并添加正文大小到响应头部。
    response_handle:headers():add("response_body_size", response_handle:body():length())
    -- 移除响应头部 ‘foo’
    response_handle:headers():remove("foo")
  end

.. code-block:: lua

  function envoy_on_request(request_handle)
    -- 使用如下头部、正文和超时时间向上游主机发起 HTTP 调用。
    local headers, body = request_handle:httpCall(
    "lua_cluster",
    {
      [":method"] = "POST",
      [":path"] = "/",
      [":authority"] = "lua_cluster"
    },
    "hello world",
    5000)

    -- 将来自 HTTP 调用的信息添加到过滤器链中即将发送的下一个过滤器上。
    request_handle:headers():add("upstream_foo", headers["foo"])
    request_handle:headers():add("upstream_body_size", #body)
  end

.. code-block:: lua

  function envoy_on_request(request_handle)
    -- 发起 HTTP 调用。
    local headers, body = request_handle:httpCall(
    "lua_cluster",
    {
      [":method"] = "POST",
      [":path"] = "/",
      [":authority"] = "lua_cluster",
      ["set-cookie"] = { "lang=lua; Path=/", "type=binding; Path=/" }
    },
    "hello world",
    5000)

    -- 直接响应并设置 HTTP 调用的头部。不会迭代到后续的过滤器。
    request_handle:respond(
      {[":status"] = "403",
       ["upstream_foo"] = headers["foo"]},
      "nope")
  end

.. code-block:: lua

  function envoy_on_request(request_handle)
    -- 记录请求信息
    request_handle:logInfo("Authority: "..request_handle:headers():get(":authority"))
    request_handle:logInfo("Method: "..request_handle:headers():get(":method"))
    request_handle:logInfo("Path: "..request_handle:headers():get(":path"))
  end

  function envoy_on_response(response_handle)
    -- 记录响应状态码
    response_handle:logInfo("Status: "..response_handle:headers():get(":status"))
  end

一个常见的使用场景是重写上游的响应正文，例如：上游发送了非 2xx 的 JSON 数据响应，但应用要求发送 HTML 页面到浏览器端。

有两种方式可以实现，第一种是通过 `body()` API。

.. code-block:: lua

    function envoy_on_response(response_handle)
      local content_length = response_handle:body():setBytes("<html><b>Not Found<b></html>")
      response_handle:headers():replace("content-length", content_length)
      response_handle:headers():replace("content-type", "text/html")
    end


或者，通过 `bodyChunks()` API，使 Envoy 跳过缓存上游的响应数据。

.. code-block:: lua

    function envoy_on_response(response_handle)

      -- 设置 content-length。
      response_handle:headers():replace("content-length", 28)
      response_handle:headers():replace("content-type", "text/html")

      local last
      for chunk in response_handle:bodyChunks() do
        -- 清除每个接收到的响应正文数据块。
        chunk:setBytes("")
        last = chunk
      end

      last:setBytes("<html><b>Not Found<b></html>")
    end

.. _config_http_filters_lua_stream_handle_api:

完整示例
----------------

:repo:`/examples/lua` 中提供了使用 Docker 的完整示例。

流处理 API
-----------------

当 Envoy 加载了脚本中的配置时，它将执行脚本中定义的两个全局方法：

.. code-block:: lua

  function envoy_on_request(request_handle)
  end

  function envoy_on_response(response_handle)
  end

脚本中可以同时定义这些方法。请求路径中，Envoy 将会以协程方式运行 *envoy_on_request*，将处理方法传递到请求 API。在响应路径中，Envoy 将以协程方式运行 *envoy_on_response*，将处理方法传递到响应 API。

.. attention::

  与 Envoy 的所有交互都要通过传递的流处理方法进行，这点至关重要。流处理方法中不应该指定任何全局变量，且不能在协程外部使用。如果处理方法被错误使用，Envoy 将使脚本失败。

支持如下的流处理方法：

headers()
^^^^^^^^^

.. code-block:: lua

  local headers = handle:headers()

返回流的头部。只要头部尚未被发送到头部链中的下一个过滤器，就可以对其进行修改。例如，在 *body()* 或 *httpCall()* 调用返回后它们可以被修改。如果在其他任何情况下修改头部，将使脚本失败。

返回一个 :ref:`头部对象 <config_http_filters_lua_header_wrapper>`。

body()
^^^^^^

.. code-block:: lua

  local body = handle:body()

返回流的正文。此调用将导致 Envoy 暂停脚本的执行直到整个正文被接收到缓冲区中。注意所有的缓冲都必须遵守适当的流控制策略。Envoy 不会缓冲超出连接管理器所允许的多出数据。

返回一个 :ref:`缓冲对象 <config_http_filters_lua_buffer_wrapper>`。

bodyChunks()
^^^^^^^^^^^^

.. code-block:: lua

  local iterator = handle:bodyChunks()

返回一个迭代器，可在所有接收到的正文块到达时用其进行迭代。在块与块之间 Envoy 将暂停执行脚本，但 *不会缓存* 它们。脚本可以用其检查流式传输的数据。

.. code-block:: lua

  for chunk in request_handle:bodyChunks() do
    request_handle:log(0, chunk:length())
  end

迭代器返回的每个块都是一个 :ref:`缓冲对象 <config_http_filters_lua_buffer_wrapper>`。

trailers()
^^^^^^^^^^

.. code-block:: lua

  local trailers = handle:trailers()

返回流的尾部。如果没有尾部则可能返回 nil。尾部在被发送到下一个过滤器前是可以被修改的。

返回一个 :ref:`头部对象 <config_http_filters_lua_header_wrapper>`。

log*()
^^^^^^

.. code-block:: lua

  handle:logTrace(message)
  handle:logDebug(message)
  handle:logInfo(message)
  handle:logWarn(message)
  handle:logErr(message)
  handle:logCritical(message)

使用 Envoy 的应用日志记录一条消息。*message* 是要记录的字符串。

httpCall()
^^^^^^^^^^

.. code-block:: lua

  local headers, body = handle:httpCall(cluster, headers, body, timeout, asynchronous)

向上游主机发起一个 HTTP 调用。*cluster* 是一个字符串，它映射到集群管理器中已配置的集群。*headers* 是要发送的键/值对表（值可以是字符串或者字符串表）。注意必须设置 *:method*、*:path* 和 *:authority* 头部。*body* 是一个可选的字符串，表示要发送的正文数据。*timeout* 是一个整型，用于指定调用的超时时间（毫秒单位）。

*asynchronous* 是一个布尔型标记。如果 asynchronous 设置为 true，无论响应成功与否，Envoy 都会发出 HTTP 请求并继续。如果此标记设置为 false，或者没设置，Envoy 将暂停执行脚本直到调用完成或者发生错误。

返回的 *headers* 是指响应头部表。返回的 *body* 是指字符串响应正文，如果没有正文则为 nil。

respond()
^^^^^^^^^^

.. code-block:: lua

  handle:respond(headers, body)

立即响应并且不再执行后续的过滤器迭代。此调用仅在请求流中生效。此外，仅当请求头部尚未传递到后续过滤器时，才可以响应。这意味着，以下 Lua 代码是无效的：

.. code-block:: lua

  function envoy_on_request(request_handle)
    for chunk in request_handle:bodyChunks() do
      request_handle:respond(
        {[":status"] = "100"},
        "nope")
    end
  end

*headers* 是要发送的键/值对表（值可以是字符串或者字符串表）。注意必须设置 *:status* 头部。*body* 是一个字符串，并提供了可选的响应正文，可能为 nil。

metadata()
^^^^^^^^^^

.. code-block:: lua

  local metadata = handle:metadata()

返回当前路由的整个元数据。注意元数据应在过滤器名称下指定，即 *envoy.filters.http.lua*。以下是 :ref:`路由条目 <envoy_v3_api_msg_config.route.v3.Route>` 中元数据的配置示例：

.. code-block:: yaml

  metadata:
    filter_metadata:
      envoy.filters.http.lua:
        foo: bar
        baz:
          - bad
          - baz

返回一个 :ref:`元数据对象 <config_http_filters_lua_metadata_wrapper>`。

streamInfo()
^^^^^^^^^^^^^

.. code-block:: lua

  local streamInfo = handle:streamInfo()

返回与当前请求相关的 :repo:`信息 <include/envoy/stream_info/stream_info.h>`。

返回一个 :ref:`流信息对象 <config_http_filters_lua_stream_info_wrapper>`。

connection()
^^^^^^^^^^^^

.. code-block:: lua

  local connection = handle:connection()

返回当前请求的底层 :repo:`连接 <include/envoy/network/connection.h>`。

返回一个 :ref:`连接对象 <config_http_filters_lua_connection_wrapper>`。

importPublicKey()
^^^^^^^^^^^^^^^^^

.. code-block:: lua

  local pubkey = handle:importPublicKey(keyder, keyderLength)

返回 :ref:`verifySignature <verify_signature>` 所使用的用于验证数字签名的公共密钥。

.. _verify_signature:

verifySignature()
^^^^^^^^^^^^^^^^^

.. code-block:: lua

  local ok, error = verifySignature(hashFunction, pubkey, signature, signatureLength, data, dataLength)

使用提供的参数验证签名。*hashFunction* 是哈希方法变量，用于验证签名，支持 *SHA1*、*SHA224*、*SHA256*、*SHA384* 和 *SHA512*。*pubkey* 是公钥。*signature* 是要验证的签名。*signatureLength* 是签名的长度。*data* 是要执行哈希计算的内容。*dataLength* 是数据长度。

该方法返回一对值。如果第一个元素值为 *true*，第二个元素值将为空，表示签名已验证；否则，第二个元素将会存储错误信息。

.. _config_http_filters_lua_stream_handle_api_base64_escape:

base64Escape()
^^^^^^^^^^^^^^
.. code-block:: lua

  local base64_encoded = handle:base64Escape("input string")

将输入字符串按 base64 编码。这在转义二进制数据时很有用。

.. _config_http_filters_lua_header_wrapper:

头部对象 API
-----------------

add()
^^^^^

.. code-block:: lua

  headers:add(key, value)

添加一个头部。*key* 是提供头部键的字符串。*value* 是提供头部值的字符串。

get()
^^^^^

.. code-block:: lua

  headers:get(key)

获取一个头部。*key* 是提供头部键的字符串。返回头部值字符串或者 nil（如果头部不存在）。

__pairs()
^^^^^^^^^

.. code-block:: lua

  for key, value in pairs(headers) do
  end

迭代每个头部。*key* 是提供头部键的字符串。*value* 是提供头部值的字符串。

.. attention::

  在当前的实现中，头部在迭代的过程中不能被修改。此外，如果有必要在迭代后修改头部。则必须首先完成迭代。这意外着不能使用 `break` 或者其他方法提前退出循环。将来的实现会更加灵活。

remove()
^^^^^^^^

.. code-block:: lua

  headers:remove(key)

移除一个头部。*key* 提供要移除的头部键。

replace()
^^^^^^^^^

.. code-block:: lua

  headers:replace(key, value)

替换一个头部。*key* 是提供头部键的字符串。*value* 是提供头部值的字符串。如果头部不存在则会按照 *add()* 方法添加头部。

.. _config_http_filters_lua_buffer_wrapper:

缓冲区 API
----------

length()
^^^^^^^^^^

.. code-block:: lua

  local size = buffer:length()

获取缓冲区的字节大小。返回一个整型。

getBytes()
^^^^^^^^^^

.. code-block:: lua

  buffer:getBytes(index, length)

获取缓冲区中的字节。默认情况下，Envoy 不会将所有缓冲区字节复制到 Lua，这将导致缓冲区段被复制。*index* 是提供缓冲区复制起始下标的整型。*length* 是提供缓冲区复制长度的整型。*index* 加 *length* 必须小于缓冲区长度。

.. _config_http_filters_lua_buffer_wrapper_api_set_bytes:

setBytes()
^^^^^^^^^^

.. code-block:: lua

  buffer:setBytes(string)

使用输入字符串设置缓冲区的封装内容。

.. _config_http_filters_lua_metadata_wrapper:

元数据对象 API
-------------------

get()
^^^^^

.. code-block:: lua

  metadata:get(key)

获取一条元数据。*key* 是提供元数据键的字符串。返回给定元数据键的相应值。值的类型可以是：*nil*、*boolean*、*number*、*string* 和 *table*。

__pairs()
^^^^^^^^^

.. code-block:: lua

  for key, value in pairs(metadata) do
  end

迭代每个 *metadata* 条目。*key* 是提供 *metadata* 键的字符串。*value* 是 *metadata* 条目的值。

.. _config_http_filters_lua_stream_info_wrapper:

流信息对象 API
-----------------------

protocol()
^^^^^^^^^^

.. code-block:: lua

  streamInfo:protocol()

返回当前请求所使用的表示 :repo:`HTTP 协议 <include/envoy/http/protocol.h>` 的字符串。可能的值为：*HTTP/1.0*、*HTTP/1.1* 和 *HTTP/2*。

dynamicMetadata()
^^^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:dynamicMetadata()

返回一个 :ref:`动态元数据对象 <config_http_filters_lua_stream_info_dynamic_metadata_wrapper>`。

downstreamSslConnection()
^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  streamInfo:downstreamSslConnection()

返回与当前 SSL 连接相关的 :repo:`信息 <include/envoy/ssl/connection.h>`。

返回一个下游 :ref:`SSL 连接信息对象 <config_http_filters_lua_ssl_socket_info>`。

.. _config_http_filters_lua_stream_info_dynamic_metadata_wrapper:

动态元数据对象 API
---------------------------

get()
^^^^^

.. code-block:: lua

  dynamicMetadata:get(filterName)

  -- 从返回的表中获取一个值。
  dynamicMetadata:get(filterName)[key]

从动态元数据结构中获取一个条目。*filterName* 是提供过滤器名称的字符串。例如 *envoy.lb*。返回与给定 *filterName* 对应的 *table*。

set()
^^^^^

.. code-block:: lua

  dynamicMetadata:set(filterName, key, value)

设置 *filterName* 的元数据键值对。*filterName* 是指定目标过滤器名称的键，例如 *envoy.lb*。*key* 的类型为 *string*。*value* 的值类型是可以映射到元数据的任何 Lua 类型：*table*、*numeric*、*boolean*、*string* 或 *nil*。当使用 *table* 作为参数时，其键只能是 *string* 或 *numeric*。

.. code-block:: lua

  function envoy_on_request(request_handle)
    local headers = request_handle:headers()
    request_handle:streamInfo():dynamicMetadata():set("envoy.filters.http.lua", "request.info", {
      auth: headers:get("authorization"),
      token: headers:get("x-request-token"),
    })
  end

  function envoy_on_response(response_handle)
    local meta = response_handle:streamInfo():dynamicMetadata():get("envoy.filters.http.lua")["request.info"]
    response_handle:logInfo("Auth: "..meta.auth..", token: "..meta.token)
  end


__pairs()
^^^^^^^^^

.. code-block:: lua

  for key, value in pairs(dynamicMetadata) do
  end

迭代每个 *dynamicMetadata* 条目。 *key* 是提供 *dynamicMetadata* 键的字符串。*value* 是一个 *dynamicMetadata* 条目值。

.. _config_http_filters_lua_connection_wrapper:

连接对象 API
---------------------

ssl()
^^^^^

.. code-block:: lua

  if connection:ssl() == nil then
    print("plain")
  else
    print("secure")
  end

当连接安全时返回 :repo:`SSL 连接 <include/envoy/ssl/connection.h>` 对象，否则返回 *nil*。

返回一个 :ref:`SSL 连接信息对象 <config_http_filters_lua_ssl_socket_info>`。

.. _config_http_filters_lua_ssl_socket_info:

SSL 连接对象 API
-------------------------

peerCertificatePresented()
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  if downstreamSslConnection:peerCertificatePresented() then
    print("peer certificate is presented")
  end

返回布尔值，表示是否存在对等证书。

peerCertificateValidated()
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  if downstreamSslConnection:peerCertificateVaidated() then
    print("peer certificate is valiedated")
  end

返回布尔值，表示对等证书是否已验证。

uriSanLocalCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  -- 例如，uriSanLocalCertificate 包含 {"san1", "san2"}
  local certs = downstreamSslConnection:uriSanLocalCertificate()

  -- 下方打印 san1,san2
  handle:logTrace(table.concat(certs, ","))

以表形式返回本地证书中 SAN 字段的 URIs。如果没有本地证书、SAN 字段或 URI SAN 条目则返回一个空表。

sha256PeerCertificateDigest()
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:sha256PeerCertificateDigest()

返回对等证书的 SHA256 摘要。如果没有可用于 TLS（非mTLS） 连接的对等证书则返回 ``""``。

serialNumberPeerCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:serialNumberPeerCertificate()

返回对等证书的序列号字段。如果没有对等证书或序列号则返回 ``""``。

issuerPeerCertificate()
^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:issuerPeerCertificate()

以 RFC 2253 格式返回对等证书的颁发者字段。如果没有对等证书或颁发者则返回 ``""``。

subjectPeerCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:subjectPeerCertificate()

以 RFC 2253 格式返回对等证书的主题字段。如果没有对等证书或主题则返回 ``""``。

uriSanPeerCertificate()
^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:uriSanPeerCertificate()

以表形式返回对等证书中 SAN 字段的 URIs。如果没有对等证书、SAN 字段或 URL SAN 条目则返回空表。

subjectLocalCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:subjectLocalCertificate()

以 RFC 2253 格式返回本地证书的主题字段。如果没有本地证书或主题则返回 ``""``。

urlEncodedPemEncodedPeerCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:urlEncodedPemEncodedPeerCertificate()

返回完整的对等证书（包括证书叶）的 URL 编码的 PEM 编码表示。如果没有对等证书或编码失败则返回 ``""``。

urlEncodedPemEncodedPeerCertificateChain()
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:urlEncodedPemEncodedPeerCertificateChain()

返回完整的对等证书链（包括证书叶）的 URL 编码的 PEM 编码表示。如果没有对等证书或编码失败则返回 ``""``。

dnsSansPeerCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:dnsSansPeerCertificate()

以表形式返回对等证书中 SAN 字段的 DNS 条目。如果没有对等证书、SAN 字段或 DNS SAN 条目则返回空表。

dnsSansLocalCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:dnsSansLocalCertificate()

以表形式返回本地证书中 SAN 字段的 DNS 条目。如果没有对等证书、SAN 字段或 DNS SAN 条目则返回空表。

validFromPeerCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:validFromPeerCertificate()

返回对等证书签发并生效的时间（以秒为单位的时间戳）。如果没有对等证书则返回 ``0``。

在 Lua 中，我们通常使用 ``os.time(os.date("!*t"))`` 获取当前的时间戳（以秒为单位）。

expirationPeerCertificate()
^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:validFromPeerCertificate()

返回对等证书过期并失效的时间（以秒为单位的时间戳）。如果没有对等证书则返回 ``0``。

在 Lua 中，我们通常使用 ``os.time(os.date("!*t"))`` 获取当前的时间戳（以秒为单位）。

sessionId()
^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:sessionId()

返回 RFC 5246 中定义的十六进制编码的 TLS 会话 ID。

ciphersuiteId()
^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:ciphersuiteId()

返回已建立的 TLS 连接中所使用的密码标准 ID（十六进制编码）。如果当前没有已商定的密码套件则返回 ``"0xffff"``。

ciphersuiteString()
^^^^^^^^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:ciphersuiteString()

返回已建立的 TLS 连接中所使用的密码套件的 OpenSSL 名称。如果当前没有已商定的密码套件则返回 ``""``。

tlsVersion()
^^^^^^^^^^^^

.. code-block:: lua

  downstreamSslConnection:urlEncodedPemEncodedPeerCertificateChain()

返回已建立的 TLS 连接中使用的 TLS 版本（例如 TLSv1.2、TLSv1.3）。

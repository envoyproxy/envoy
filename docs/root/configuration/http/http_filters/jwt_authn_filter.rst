.. _config_http_filters_jwt_authn:

JWT 认证
==================

此 HTTP 过滤器可用于验证 JSON Web Token（JWT）。它将验证其签名、受众（audience）和发行人。它还将检查其时间限制，例如到期时间和 nbf（not before 不早于）时间。如果 JWT 验证失败，其请求将被拒绝。如果 JWT 验证成功，则可以将其有效负载转发到上游，以按需进行进一步授权。

需要 JWKS 来验证 JWT 签名。它们可以在过滤器配置中指定，也可以从 JWKS 服务器远程获取。

以下是支持的 JWT alg：

.. code-block::

   ES256, ES384, ES512,
   HS256, HS384, HS512,
   RS256, RS384, RS512,
   PS256, PS384, PS512,
   EdDSA

配置
--------

此过滤器应的名称应该配置为 *envoy.filters.http.jwt_authn*。

此 HTTP :ref:`过滤器配置 <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.JwtAuthentication>` 包含两个字段：

* 字段 *providers* 指定应如何验证 JWT，例如在哪里提取令牌，在哪里获取公共密钥（JWKS）以及在何处输出其有效负载。
* 字段 *rules* 指定匹配的规则及其 requirements。如果请求符合规则，则应用其 requirement。该 requirement 指定应使用哪些 JWT providers。

JwtProvider
~~~~~~~~~~~

:ref:`JwtProvider <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.JwtProvider>` 指定应如何验证 JWT。它具有以下字段：

* *issuer*: 发行 JWT 的主体，通常是 URL 或电子邮件地址。
* *audiences*: 允许访问的 JWT 受众列表。包含任何这些受众的 JWT 将被接受。如果未指定，将不检查 JWT 中的受众。
* *local_jwks*: 在本地数据源中获取 JWKS，可以在本地文件中或嵌入在内联字符串中。
* *remote_jwks*: 从远程 HTTP 服务器获取 JWKS，还可以指定缓存持续时间。
* *forward*: 如果为 true，则将 JWT 转发到上游。
* *from_headers*: 从 HTTP 头部中提取 JWT。
* *from_params*: 从查询参数中提取 JWT。
* *forward_payload_header*: 在指定的 HTTP 头部中转发 JWT 有效负载。

默认提取位置
~~~~~~~~~~~~~~~~~~~~~~~~

如果 *from_headers* 和 *from_params* 为空，则默认从 HTTP 头部提取 JWT：

  Authorization: Bearer <token>

和查询参数的 key *access_token* ::

  /path?access_token=<JWT>

如果一个请求有两个 token，一个来自 HTTP 头部，另一个来自 HTTP 查询参数，则所有这些 token 都必须有效。

在 :ref:`过滤器配置中 <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.JwtAuthentication>`，*providers* 是一个映射，用于将 *provider_name* 映射到 :ref:`JwtProvider <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.JwtProvider>`。*provider_name* 必须是唯一的，它被 :ref:`JwtRequirement <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.JwtRequirement>` 中的 *provider_name* 字段引用。

.. important::
   对于 *remote_jwks*，**jwks_cluster** cluster 字段是必须提供的。

由于上述要求，`OpenID Connect 发现 <https://openid.net/specs/openid-connect-discovery-1_0.html>`_ 是不支持的，因为要获取 JWKS 的 URL 是基于响应发现的。为动态 URL 设置集群配置并不容易。

远程 JWKS 配置示例
~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: yaml

  providers:
    provider_name1:
      issuer: https://example.com
      audiences:
      - bookstore_android.apps.googleusercontent.com
      - bookstore_web.apps.googleusercontent.com
      remote_jwks:
        http_uri:
          uri: https://example.com/jwks.json
          cluster: example_jwks_cluster
        cache_duration:
          seconds: 300

上面的示例使用 URL https://example.com/jwks.json 从远程服务器获取 JWSK。令牌将从默认提取位置提取。令牌不会转发到上游。JWT 有效负载不会添加到请求头部中。

需要以下 cluster **example_jwks_cluster** 来获取 JWKS。

.. code-block:: yaml

  cluster:
    name: example_jwks_cluster
    type: STRICT_DNS
    load_assignment:
      cluster_name: example_jwks_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: example.com
                port_value: 80


内联 JWKS 配置示例
~~~~~~~~~~~~~~~~~~~~~~~~~~

使用内联 JWKS 的另一个配置示例：

.. code-block:: yaml

  providers:
    provider_name2:
      issuer: https://example2.com
      local_jwks:
        inline_string: PUBLIC-KEY
      from_headers:
      - name: jwt-assertion
      forward: true
      forward_payload_header: x-jwt-payload

上面的示例使用内联字符指定 JWKS。JWT 令牌将从下面的 HTTP 头部中提取：

     jwt-assertion: <JWT>.

JWT 有效负载将以以下格式添加到请求头部：

    x-jwt-payload: base64url_encoded(jwt_payload_in_JSON)

RequirementRule
~~~~~~~~~~~~~~~

:ref:`RequirementRule <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.RequirementRule>` 具有两个字段：

* 字段 *match* 指定如何匹配请求；例如通过 HTTP 头部，查询参数或路径前缀。
* 字段 *requires* 指定 JWT requirement，例如需要哪个 provider。

.. important::
   - **如果一个请求匹配多个规则，则将应用第一个匹配的规则。**.
   - 如果匹配规则的 *requires* 字段为空，**则不需要 JWT 验证**。
   - 如果请求不符合任何规则，**则不需要 JWT 验证**。

单一 requirement 配置示例
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: yaml

  providers:
    jwt_provider1:
      issuer: https://example.com
      audiences:
        audience1
      local_jwks:
        inline_string: PUBLIC-KEY
  rules:
  - match:
      prefix: /health
  - match:
      prefix: /api
    requires:
      provider_and_audiences:
        provider_name: jwt_provider1
        audiences:
          api_audience
  - match:
      prefix: /
    requires:
      provider_name: jwt_provider1

上面的配置使用单个 requirement 规则，每个规则可以具有空 requirement 或具有一个 provider 名称的单个 requirement。

组 requirement 配置示例
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: yaml

  providers:
    provider1:
      issuer: https://provider1.com
      local_jwks:
        inline_string: PUBLIC-KEY
    provider2:
      issuer: https://provider2.com
      local_jwks:
        inline_string: PUBLIC-KEY
  rules:
  - match:
      prefix: /any
    requires:
      requires_any:
        requirements:
        - provider_name: provider1
        - provider_name: provider2
  - match:
      prefix: /all
    requires:
      requires_all:
        requirements:
        - provider_name: provider1
        - provider_name: provider2

上面的配置使用更复杂的*组* requirements：

* 第一条 *rule* 指定 *requires_any*；如果满足 **provider1** 或 **provider2** 的 requirement，请求可以继续。
* 第二条 *rule* 指定 *requires_all*；只有同时满足 **provider1** 和 **provider2** 的 requirements，请求才能继续。

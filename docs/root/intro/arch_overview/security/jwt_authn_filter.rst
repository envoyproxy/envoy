.. _arch_overview_jwt_authn:

JSON Web Token（JWT）认证
===================================

* :ref:`HTTP 过滤器配置 <config_http_filters_jwt_authn>`。

JSON Web Token (JWT) 认证过滤器检查传入的请求是否具有有效的 JSON Web Token (JWT)。它通过基于 :ref:`HTTP 过滤器配置 <config_http_filters_jwt_authn>` 来验证 JWT 签名、受众和发行者来检查 JWT 的有效性。JWT 认证过滤器可以配置为立即拒绝无效的 JWT 请求，或者继续将 JWT 有效负载传递给其它过滤器，由后续的过滤器来决定是否拒绝。

JWT 认证过滤器支持在请求的各种条件下检查 JWT，可以将其配置为仅在特定路径上检查 JWT，这样您就可以允许列出来自 JWT 认证的一些路径，如果路径是可公开访问的，并且不需要任何 JWT 认证，那么这将非常有用。

JWT 认证过滤器支持从请求的不同位置提取 JWT，并且可以合并针对同一请求的多个 JWT 要求。JWT 签名验证所需的 `JSON Web Key Set (JWKS) <https://tools.ietf.org/html/rfc7517>`_ 可以在过滤器配置中内联指定，也可以通过 HTTP/HTTPS 从远程服务器获取。

JWT 认证过滤器还支持将成功验证的 JWT 的有效载荷写入 :ref:`动态状态 <arch_overview_data_sharing_between_filters>`，以便后续的过滤器可以使用它来基于 JWT 有效负载做出自己的决定。

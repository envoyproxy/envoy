
.. _config_http_filters_oauth:

OAuth2
======

* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.oauth2.v3alpha.OAuth2>`
* 此过滤器的配置项名称应为：*envoy.filters.http.oauth2*。

.. attention::

  OAuth2 过滤器目前处于活跃的开发状态。

示例配置
---------------------

.. code-block::

   http_filters:
   - name: oauth2
     typed_config:
       "@type": type.googleapis.com/envoy.extensions.filters.http.oauth2.v3alpha.OAuth2
       token_endpoint:
         cluster: oauth
         uri: oauth.com/token
         timeout: 3s
       authorization_endpoint: https://oauth.com/oauth/authorize/
       redirect_uri: "%REQ(:x-forwarded-proto)%://%REQ(:authority)%/callback"
       redirect_path_matcher:
         path:
           exact: /callback
       signout_path:
         path:
           exact: /signout
      credentials:
        client_id: foo
        token_secret:
          name: token
        hmac_secret:
          name: hmac
      timeout: 3s
   - name: envoy.router

  clusters:
  - name: service
    ...
  - name: auth
    connect_timeout: 5s
    type: LOGICAL_DNS
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: auth
      endpoints:
      - lb_endpoints:
        - endpoint:
            address: { socket_address: { address: auth.example.com, port_value: 443 }}
    tls_context: { sni: auth.example.com }

注意
-----

这个模块目前没有为发送/返回到 OAuth 服务器的重定向循环提供太多的跨站请求伪造（ Cross-Site-Request-Forgery）方面的保护。

为了保障过滤器正常运行，服务必须基于 HTTPS，且 cookies 须使用 `;secure`。

统计
----------

OAuth 过滤器在 *<stat_prefix>.* 命名空间下输出统计信息。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  oauth_failure, Counter, 拒绝请求总数。
  oauth_success, Counter, 允许的请求总数。
  oauth_unauthorization_rq, Counter, 未授权请求总数。

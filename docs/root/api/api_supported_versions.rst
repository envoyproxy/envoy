.. _api_supported_versions:

支持的 API 版本
=================

Envoy 的 API 遵循 :repo:`版本方案 <api/API_VERSIONING.md>`，其中 Envoy 在任何时候都支持多个主要的 API 版本。下面就是目前为止支持的版本：

* :ref:`v2 xDS API <envoy_api_reference>` （*弃用的*, 在 2020 年废弃）。此 API 在 2020 年第一季度后，将不再接受新的功能特性。
* :ref:`v3 xDS API <envoy_v3_api_reference>` （*正在使用的*, 废弃时间未知）。Envoy 鼓励开发人员和维护人员积极采用 v3 xDS 并积极使用。

Envoy 不再支持下述的 API 版本：

* 这是在当前 Protobuf 和双 REST/gRPC xDS API 之前的老旧 REST-JSON API。

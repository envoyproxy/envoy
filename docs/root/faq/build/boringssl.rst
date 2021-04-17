为什么 Envoy 使用 BoringSSL？
=============================

`BoringSSL <https://boringssl.googlesource.com/boringssl/>`_ 是由 Google 维护的精简 TLS 实现。 正确设置 TLS 是非常困难的。Envoy 选择与 BoringSSL 保持一致，以便于获得 Google 聘用的世界一流专家的支持。简而言之： 如果 BoringSSL 对 Google 的生产系统足够好的话，那它对 Envoy 也足够好，那么该项目将不会为任何其他 TLS 的实现提供一级的支持。

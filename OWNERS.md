* See [CONTRIBUTING.md](CONTRIBUTING.md) for general contribution guidelines.
* See [GOVERNANCE.md](GOVERNANCE.md) for governance guidelines and maintainer responsibilities.
* See [CODEOWNERS](CODEOWNERS) for a detailed list of owners for the various source directories.

This page lists all active maintainers and their areas of expertise. This can be used for
routing PRs, questions, etc. to the right place.

# Senior maintainers
<!--- If you modify senior maintainers list, please update the core-maintainers section of SECURITY-INSIGHTS.yml  -->

* Matt Klein ([mattklein123](https://github.com/mattklein123)) (mattklein123@gmail.com)
  * Catch-all, "all the things", and generally trying to make himself obsolete as fast as
    possible.
* Alyssa Wilk ([alyssawilk](https://github.com/alyssawilk)) (alyssar@google.com)
  * HTTP, flow control, cluster manager, load balancing, and core networking (listeners,
    connections, etc.), Envoy Mobile.
* Stephan Zuercher ([zuercher](https://github.com/zuercher)) (zuercher@gmail.com)
  * Load balancing, upstream clusters and cluster manager, logging, complex HTTP routing
    (metadata, etc.), and macOS build.
* Greg Greenway ([ggreenway](https://github.com/ggreenway)) (ggreenway@apple.com)
  * TLS, TCP proxy, listeners, and HTTP proxy/connection pooling.
* Yan Avlasov ([yanavlasov](https://github.com/yanavlasov)) (yavlasov@google.com)
  * Data plane, codecs, security, configuration.
* Ryan Northey ([phlax](https://github.com/phlax)) (ryan@synca.io)
  * Docs, tooling, CI, containers and sandbox examples
* Ryan Hamilton ([RyanTheOptimist](https://github.com/ryantheoptimist)) (rch@google.com)
  * HTTP/3, upstream connection management, Envoy Mobile.
* Baiping Wang ([wbpcode](https://github.com/wbpcode)) (wbphub@live.com)
  * Upstream, LB, tracing, logging, performance, and generic/dubbo proxy.

# Maintainers
<!--- If you modify maintainers list, please update the core-maintainers section of SECURITY-INSIGHTS.yml -->

* Joshua Marantz ([jmarantz](https://github.com/jmarantz)) (jmarantz@google.com)
  * Stats, abseil, scalability, and performance.
* Adi Peleg ([adisuissa](https://github.com/adisuissa)) (adip@google.com)
  * xDS APIs, configuration, control plane, fuzzing.
* Kevin Baichoo ([KBaichoo](https://github.com/KBaichoo)) (envoy@kevinbaichoo.com)
  * Data plane, overload management, flow control.
* Keith Smiley ([keith](https://github.com/keith)) (keithbsmiley@gmail.com)
  * Bazel, CI, compilers, linkers, general build issues, etc.
* Kuat Yessenov ([kyessenov](https://github.com/kyessenov)) (kuat@google.com)
  * Listeners, RBAC, CEL, matching, Istio.
* Raven Black ([ravenblackx](https://github.com/ravenblackx)) (ravenblack@dropbox.com)
  * Caches, file filters, and file I/O.
* Alex Xu ([soulxu](https://github.com/soulxu)) (hejie.xu@intel.com)
  * Listeners, iouring, data plane.
* Kateryna Nezdolii ([nezdolik](https://github.com/nezdolik)) (kateryna.nezdolii@gmail.com)
  * Load balancing, GeoIP, overload manager, security.
* Tianyu Xia ([tyxia](https://github.com/tyxia)) (tyxia@google.com)
  * ext_proc, data plane, flow control, CEL.

# Envoy mobile maintainers

The following Envoy maintainers have final say over any changes only affecting /mobile

* Ali Beyad ([abeyad](https://github.com/abeyad)) (abeyad@google.com)
  * xDS, C++ integration tests.
* Fredy Wijaya ([fredyw](https://github.com/fredyw)) (fredyw@google.com)
  * Android, Java, Kotlin, JNI.

# Senior extension maintainers

The following extension maintainers have final say over the extensions mentioned below. Once they
approve an extension PR, it will be merged by the maintainer on-call (or any other maintainer)
without further review.

* Michael Warres ([mpwarres] (https://github.com/mpwarres)) (mpw@google.com)
  * Wasm
* doujiang24 ([doujiang24] https://github.com/doujiang24) (doujiang24@gmail.com)
  * Golang
* Lizan Zhou ([lizan](https://github.com/lizan)) (lizan.j@gmail.com)
  * Wasm, JWT, gRPC-JSON transcoder

# Envoy security team

* All senior maintainers
* Tony Allen ([tonya11en](https://github.com/tonya11en)) (tony@allen.gg)
* Tim Walsh ([twghu](https://github.com/twghu)) (twalsh@redhat.com)
* Pradeep Rao ([pradeepcrao](https://github.com/pradeepcrao)) (pcrao@google.com)
* Kateryna Nezdolii ([nezdolik](https://github.com/nezdolik)) (kateryna.nezdolii@gmail.com)
* Boteng Yao ([botengyao](https://github.com/botengyao)) (boteng@google.com)
* Kevin Baichoo ([KBaichoo](https://github.com/KBaichoo)) (envoy@kevinbaichoo.com)
* Tianyu Xia ([tyxia](https://github.com/tyxia)) (tyxia@google.com)
* Kirtimaan Rajshiva ([krajshiva](https://github.com/krajshiva))

# Emeritus maintainers

* Constance Caramanolis ([ccaraman](https://github.com/ccaraman)) (ccaramanolis@lyft.com)
* Roman Dzhabarov ([RomanDzhabarov](https://github.com/RomanDzhabarov)) (rdzhabarov@lyft.com)
* Bill Gallagher ([wgallagher](https://github.com/wgallagher)) (bgallagher@lyft.com)
* Dan Noé ([dnoe](https://github.com/dnoe)) (dpn@google.com)
* Sotiris Nanopoulos ([davinci26](https://github.com/davinci26)) (Sotiris.Nanopoulos@microsoft.com)
* Asra Ali ([asraa](https://github.com/asraa)) (asraa@google.com)
* Jose Nino ([junr03](https://github.com/junr03)) (recruiting@junr03.com)
* Dhi Aurrahman ([dio](https://github.com/dio)) (dio@rockybars.com)
* Dmitry Rozhkov ([rojkov](https://github.com/rojkov)) (dmitry.rozhkov@intel.com)
* Michael Rebello ([rebello95](https://github.com/rebello95)) (mrebello@lyft.com)
* Alan Chiu ([buildbreaker](https://github.com/buildbreaker)) (achiu@lyft.com)
* Charles Le Borgne ([carloseltuerto](https://github.com/carloseltuerto)) (cleborgne@google.com)
* William A Rowe Jr ([wrowe](https://github.com/wrowe)) (wrowe@rowe-clan.net)
* Antonio Vicente ([antoniovicente](https://github.com/antoniovicente)) (avd@google.com)
* JP Simard ([jpsim](https://github.com/jpsim)) (jp@lyft.com)
* Rafal Augustyniak ([Augustyniak](https://github.com/Augustyniak)) (raugustyniak@lyft.com)
* Snow Pettersen ([snowp](https://github.com/snowp)) (aickck@gmail.com)
* Lizan Zhou ([lizan](https://github.com/lizan)) (lizan.j@gmail.com)
* Harvey Tuch ([htuch](https://github.com/htuch)) (htuch@google.com)

# Friends of Envoy

This section lists a few people that are not maintainers but typically help out with subject
matter expert reviews. Feel free to loop them in as needed.

* Yuchen Dai ([lambdai](https://github.com/lambdai)) (lambdai@google.com)
  * v2 xDS, listeners, filter chain discovery service.
* Michael Payne ([moderation](https://github.com/moderation)) (m@m17e.org)
  * External dependencies, Envoy's supply chain and documentation.
* Cerek Hillen ([crockeo](https://github.com/crockeo)) (chillen@lyft.com)
  * Python and C++ platform bindings.

import Envoy
import EnvoyEngine
import Foundation
import XCTest

final class QUICStreamTests: XCTestCase {
  func testQUICStream() throws {
    // swiftlint:disable:next line_length
    let hcmType = "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager"
    // swiftlint:disable:next line_length
    let quicDownstreamType = "type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicDownstreamTransport"
    // swiftlint:disable:next line_length
    let quicUpstreamType = "type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicUpstreamTransport"
    let config =
    """
    static_resources:
      listeners:
      - name: h3_remote_listener
        address:
          socket_address: { protocol: UDP, address: 127.0.0.1, port_value: 10101 }
        reuse_port: true
        udp_listener_config:
          quic_options: {}
          downstream_socket_config:
            prefer_gro: true
        filter_chains:
          transport_socket:
            name: envoy.transport_sockets.quic
            typed_config:
              "@type": \(quicDownstreamType)
              downstream_tls_context:
                common_tls_context:
                  alpn_protocols: h3
                  tls_certificates:
                    certificate_chain:
                      inline_string: |
                        -----BEGIN CERTIFICATE-----
                        MIIEbDCCA1SgAwIBAgIUJuVBh0FKfFgIcO++ljWm7D47eYUwDQYJKoZIhvcNAQEL
                        BQAwdjELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNhbGlmb3JuaWExFjAUBgNVBAcM
                        DVNhbiBGcmFuY2lzY28xDTALBgNVBAoMBEx5ZnQxGTAXBgNVBAsMEEx5ZnQgRW5n
                        aW5lZXJpbmcxEDAOBgNVBAMMB1Rlc3QgQ0EwHhcNMjAwODA1MTkxNjAxWhcNMjIw
                        ODA1MTkxNjAxWjCBpjELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNhbGlmb3JuaWEx
                        FjAUBgNVBAcMDVNhbiBGcmFuY2lzY28xDTALBgNVBAoMBEx5ZnQxGTAXBgNVBAsM
                        EEx5ZnQgRW5naW5lZXJpbmcxGjAYBgNVBAMMEVRlc3QgQmFja2VuZCBUZWFtMSQw
                        IgYJKoZIhvcNAQkBFhViYWNrZW5kLXRlYW1AbHlmdC5jb20wggEiMA0GCSqGSIb3
                        DQEBAQUAA4IBDwAwggEKAoIBAQC9JgaI7hxjPM0tsUna/QmivBdKbCrLnLW9Teak
                        RH/Ebg68ovyvrRIlybDT6XhKi+iVpzVY9kqxhGHgrFDgGLBakVMiYJ5EjIgHfoo4
                        UUAHwIYbunJluYCgANzpprBsvTC/yFYDVMqUrjvwHsoYYVm36io994k9+t813b70
                        o0l7/PraBsKkz8NcY2V2mrd/yHn/0HAhv3hl6iiJme9yURuDYQrae2ACSrQtsbel
                        KwdZ/Re71Z1awz0OQmAjMa2HuCop+Q/1QLnqBekT5+DH1qKUzJ3Jkq6NRkERXOpi
                        87j04rtCBteCogrO67qnuBZ2lH3jYEMb+lQdLkyNMLltBSdLAgMBAAGjgcAwgb0w
                        DAYDVR0TAQH/BAIwADALBgNVHQ8EBAMCBeAwHQYDVR0lBBYwFAYIKwYBBQUHAwIG
                        CCsGAQUFBwMBMEEGA1UdEQQ6MDiGHnNwaWZmZTovL2x5ZnQuY29tL2JhY2tlbmQt
                        dGVhbYIIbHlmdC5jb22CDHd3dy5seWZ0LmNvbTAdBgNVHQ4EFgQU2XcTZbc0xKZf
                        gNVKSvAbMZJCBoYwHwYDVR0jBBgwFoAUlkvaLFO0vpXGk3Pip6SfLg1yGIcwDQYJ
                        KoZIhvcNAQELBQADggEBAFW05aca3hSiEz/g593GAV3XP4lI5kYUjGjbPSy/HmLr
                        rdv/u3bGfacywAPo7yld+arMzd35tIYEqnhoq0+/OxPeyhwZXVVUatg5Oknut5Zv
                        2+8l+mVW+8oFCXRqr2gwc8Xt4ByYN+HaNUYfoucnjDplOPukkfSuRhbxqnkhA14v
                        Lri2EbISX14sXf2VQ9I0dkm1hXUxiO0LlA1Z7tvJac9zPSoa6Oljke4D1iH2jzwF
                        Yn7S/gGvVQgkTmWrs3S3TGyBDi4GTDhCF1R+ESvXz8z4UW1MrCSdYUXbRtsT7sbE
                        CjlFYuUyxCi1oe3IHCeXVDo/bmzwGQPDuF3WaDNSYWU=
                        -----END CERTIFICATE-----
                    private_key:
                      inline_string: |
                        -----BEGIN RSA PRIVATE KEY-----
                        MIIEpAIBAAKCAQEAvSYGiO4cYzzNLbFJ2v0JorwXSmwqy5y1vU3mpER/xG4OvKL8
                        r60SJcmw0+l4Sovolac1WPZKsYRh4KxQ4BiwWpFTImCeRIyIB36KOFFAB8CGG7py
                        ZbmAoADc6aawbL0wv8hWA1TKlK478B7KGGFZt+oqPfeJPfrfNd2+9KNJe/z62gbC
                        pM/DXGNldpq3f8h5/9BwIb94ZeooiZnvclEbg2EK2ntgAkq0LbG3pSsHWf0Xu9Wd
                        WsM9DkJgIzGth7gqKfkP9UC56gXpE+fgx9ailMydyZKujUZBEVzqYvO49OK7QgbX
                        gqIKzuu6p7gWdpR942BDG/pUHS5MjTC5bQUnSwIDAQABAoIBADEMwlcSAFSPuNln
                        hzJ9udj0k8md4T8p5Usw/2WLyeJDdBjg30wjQniAJBXgDmyueWMNmFz4iYgdP1CG
                        /vYOEPV7iCZ7Da/TDZd77hYKo+MevuhD4lSU1VEoyCDjNA8OxKyHJB77BwmlYS+0
                        nE3UOPLji47EOVfUTbvnRBSmn3DCSHkQiRIUP1xMivoiZgKJn+D+FxSMwwiq2pQR
                        5tdo7nh2A8RxlYUbaD6i4poUB26HVm8vthXahNEkLpXQOz8MWRzs6xOdDHRzi9kT
                        ItRLa4A/3LIATqviQ2EpwcALHXcULcNUMTHORC1EHPvheWR5nLuRllYzN4ReoeHC
                        3+A5KEkCgYEA52rlh/22/rLckCWugjyJic17vkg46feSOGhjuP2LelrIxNlg491y
                        o28n8lQPSVnEp3/sT7Y3quVvdboq4DC9LTzq52f6/mCYh9UQRpljuSmFqC2MPG46
                        Zl5KLEVLzhjC8aTWkhVINSpz9vauXderOpFYlPW32lnRTjJWE276kj8CgYEA0T2t
                        ULnn7TBvRSpmeWzEBA5FFo2QYkYvwrcVe0pfUltV6pf05xUmMXYFjpezSTEmPhh6
                        +dZdhwxDk+6j8Oo61rTWucDsIqMj5ZT1hPNph8yQtb5LRlRbLGVrirU9Tp7xTgMq
                        3uRA2Eka1d98dDBsEbMIVFSZ2MX3iezSGRL6j/UCgYEAxZQ82HjEDn2DVwb1EXjC
                        LQdliTZ8cTXQf5yQ19aRiSuNkpPN536ga+1xe7JNQuEDx8auafg3Ww98tFT4WmUC
                        f2ctX9klMJ4kXISK2twHioVq+gW5X7b04YXLajTX3eTCPDHyiNLmzY2raMWAZdrG
                        9MA3kyafjCt3Sn4rg3gTM10CgYEAtJ8WRpJEd8aQttcUIItYZdvfnclUMtE9l0su
                        GwCnalN3xguol/X0w0uLHn0rgeoQhhfhyFtY3yQiDcg58tRvODphBXZZIMlNSnic
                        vEjW9ygKXyjGmA5nqdpezB0JsB2aVep8Dm5g35Ozu52xNCc8ksbGUO265Jp3xbMN
                        5iEw9CUCgYBmfoPnJwzA5S1zMIqESUdVH6p3UwHU/+XTY6JHAnEVsE+BuLe3ioi7
                        6dU4rFd845MCkunBlASLV8MmMbod9xU0vTVHPtmANaUCPxwUIxXQket09t19Dzg7
                        A23sE+5myXtcfz6YrPhbLkijV4Nd7fmecodwDckvpBaWTMrv52/Www==
                        -----END RSA PRIVATE KEY-----
          filters:
          - name: envoy.filters.network.http_connection_manager
            typed_config:
              "@type": \(hcmType)
              codec_type: HTTP3
              stat_prefix: remote_hcm
              route_config:
                name: remote_route
                virtual_hosts:
                - name: remote_service
                  domains: ["*"]
                  routes:
                  - match: { prefix: "/" }
                    direct_response: { status: 200 }
              http3_protocol_options:
              http_filters:
              - name: envoy.router
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
      - name: base_api_listener
        address:
          socket_address: { protocol: TCP, address: 0.0.0.0, port_value: 10000 }
        api_listener:
          api_listener:
            "@type": \(hcmType)
            stat_prefix: api_hcm
            route_config:
              name: api_router
              virtual_hosts:
              - name: api
                domains: ["*"]
                routes:
                - match: { prefix: "/" }
                  route: { host_rewrite_literal: lyft.com, cluster: h3_remote }
            http_filters:
            - name: envoy.router
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
      clusters:
      - name: h3_remote
        connect_timeout: 10s
        type: STATIC
        dns_lookup_family: V4_ONLY
        lb_policy: ROUND_ROBIN
        load_assignment:
          cluster_name: h3_remote
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address: { address: 127.0.0.1, port_value: 10101 }
        typed_extension_protocol_options:
          envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
            "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
            explicit_http_config:
              http3_protocol_options: {}
            common_http_protocol_options:
              idle_timeout: 1s
        transport_socket:
          name: envoy.transport_sockets.quic
          typed_config:
            "@type": \(quicUpstreamType)
            upstream_tls_context:
              sni: lyft.com
    """
    let expectation = self.expectation(description: "Complete response received.")

    let client = EngineBuilder(yaml: config)
      .addLogLevel(.trace)
      .build()
      .streamClient()

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "https",
                                               authority: "lyft.com", path: "/test")
      .build()

    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream, _ in
        XCTAssertEqual(200, responseHeaders.httpStatus)
        if endStream {
          expectation.fulfill()
        }
      }
      .setOnResponseData { _, endStream, _ in
        if endStream {
          expectation.fulfill()
        }
      }
      .setOnError { _, _ in
        XCTFail("Unexpected error")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [expectation], timeout: 1), .completed)
  }
}

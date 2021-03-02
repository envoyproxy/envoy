import Envoy
import Foundation
import XCTest

final class GRPCRequestHeadersBuilderTests: XCTestCase {
  func testAddsSchemeToHeaders() {
    let headers = GRPCRequestHeadersBuilder(scheme: "https",
                                            authority: "envoyproxy.io",
                                            path: "/pb.api.v1.Foo/GetBar")
      .build()
    XCTAssertEqual(["https"], headers.value(forName: ":scheme"))
    XCTAssertEqual("https", headers.scheme)
  }

  func testAddsAuthorityToHeaders() {
    let headers = GRPCRequestHeadersBuilder(scheme: "https",
                                            authority: "envoyproxy.io",
                                            path: "/pb.api.v1.Foo/GetBar")
      .build()
    XCTAssertEqual(["envoyproxy.io"], headers.value(forName: ":authority"))
    XCTAssertEqual("envoyproxy.io", headers.authority)
  }

  func testAddsPathToHeaders() {
    let headers = GRPCRequestHeadersBuilder(scheme: "https",
                                            authority: "envoyproxy.io",
                                            path: "/pb.api.v1.Foo/GetBar")
      .build()
    XCTAssertEqual(["/pb.api.v1.Foo/GetBar"], headers.value(forName: ":path"))
    XCTAssertEqual("/pb.api.v1.Foo/GetBar", headers.path)
  }

  func testAddsGRPCContentTypeHeader() {
    let headers = GRPCRequestHeadersBuilder(scheme: "https",
                                            authority: "envoyproxy.io",
                                            path: "/pb.api.v1.Foo/GetBar")
      .build()
    XCTAssertEqual(["application/grpc"], headers.value(forName: "content-type"))
  }

  func testAddsH2UpstreamHeader() {
    let headers = GRPCRequestHeadersBuilder(scheme: "https",
                                            authority: "envoyproxy.io",
                                            path: "/pb.api.v1.Foo/GetBar")
      .build()
    XCTAssertEqual(["http2"], headers.value(forName: "x-envoy-mobile-upstream-protocol"))
  }

  func testUsesHTTPPOST() {
    let headers = GRPCRequestHeadersBuilder(scheme: "https",
                                            authority: "envoyproxy.io",
                                            path: "/pb.api.v1.Foo/GetBar")
      .build()
    XCTAssertEqual(.post, headers.method)
    XCTAssertEqual(["POST"], headers.value(forName: ":method"))
  }

  func testAddsTimeoutHeaderWhenSetToValue() {
    let headers = GRPCRequestHeadersBuilder(scheme: "https",
                                            authority: "envoyproxy.io",
                                            path: "/pb.api.v1.Foo/GetBar")
      .addTimeoutMs(200)
      .build()
    XCTAssertEqual(["200m"], headers.value(forName: "grpc-timeout"))
  }

  func testRemovesTimeoutHeaderWhenSetToNil() {
    let headers = GRPCRequestHeadersBuilder(scheme: "https",
                                            authority: "envoyproxy.io",
                                            path: "/pb.api.v1.Foo/GetBar")
      .addTimeoutMs(200)
      .addTimeoutMs(nil)
      .build()
    XCTAssertNil(headers.value(forName: "grpc-timeout"))
  }
}

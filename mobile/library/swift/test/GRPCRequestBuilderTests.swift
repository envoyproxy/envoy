@testable import Envoy
import Foundation
import XCTest

final class GRPCRequestBuilderTests: XCTestCase {
  func testUsesCorrectAuthorityWithHTTPS() {
    let request = GRPCRequestBuilder(path: "/pb.api.v1.Foo/GetBar",
                                     authority: "foo.bar.com",
                                     useHTTPS: true)
      .build()
    XCTAssertEqual("https", request.scheme)
  }

  func testUsesCorrectAuthorityWithHTTP() {
    let request = GRPCRequestBuilder(path: "/pb.api.v1.Foo/GetBar",
                                     authority: "foo.bar.com",
                                     useHTTPS: false)
      .build()
    XCTAssertEqual("http", request.scheme)
  }

  func testAddsGRPCContentTypeHeader() {
    let request = GRPCRequestBuilder(path: "/pb.api.v1.Foo/GetBar",
                                     authority: "foo.bar.com")
      .build()
    XCTAssertEqual(["application/grpc"], request.headers["content-type"])
  }

  func testAddsH2UpstreamHeader() {
    let headers = GRPCRequestBuilder(path: "/pb.api.v1.Foo/GetBar",
                                     authority: "foo.bar.com")
      .build()
      .outboundHeaders()
    XCTAssertEqual(["http2"], headers["x-envoy-mobile-upstream-protocol"])
  }

  func testUsesHTTPPOST() {
    let request = GRPCRequestBuilder(path: "/pb.api.v1.Foo/GetBar",
                                     authority: "foo.bar.com")
      .build()
    XCTAssertEqual(.post, request.method)
  }

  func testAddsTimeoutHeaderWhenSetToValue() {
    let request = GRPCRequestBuilder(path: "/pb.api.v1.Foo/GetBar",
                                     authority: "foo.bar.com")
      .addTimeoutMS(200)
      .build()
    XCTAssertEqual(["200m"], request.headers["grpc-timeout"])
  }

  func testRemovesTimeoutHeaderWhenSetToNil() {
    let request = GRPCRequestBuilder(path: "/pb.api.v1.Foo/GetBar",
                                     authority: "foo.bar.com")
      .addTimeoutMS(200)
      .addTimeoutMS(nil)
      .build()
    XCTAssertNil(request.headers["grpc-timeout"])
  }
}

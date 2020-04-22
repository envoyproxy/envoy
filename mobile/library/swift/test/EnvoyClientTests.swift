@testable import Envoy
import Foundation
import XCTest

final class EnvoyClientTests: XCTestCase {
  private var envoy: EnvoyClient!

  override func setUp() {
    super.setUp()
    self.envoy = try! EnvoyClientBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .build()
  }

  override func tearDown() {
    super.tearDown()
    MockEnvoyHTTPStream.reset()
  }

  // MARK: - Streaming

  func testStartingAStreamSendsHeaders() {
    let expectation = self.expectation(description: "Sends stream headers")
    let expectedHeaders = [
      "key_1": ["value_a"],
      ":method": ["POST"],
      ":scheme": ["https"],
      ":authority": ["www.envoyproxy.io"],
      ":path": ["/test"],
    ]

    MockEnvoyHTTPStream.onHeaders = { headers, closeStream in
      XCTAssertEqual(expectedHeaders, headers)
      XCTAssertFalse(closeStream)
      expectation.fulfill()
    }

    let request = RequestBuilder(
      method: .post, scheme: "https", authority: "www.envoyproxy.io", path: "/test")
      .addHeader(name: "key_1", value: "value_a")
      .build()
    _ = self.envoy.start(request, handler: ResponseHandler())
    self.wait(for: [expectation], timeout: 0.1)
  }

  func testSendingDataOnStreamPassesDataToTheUnderlyingStream() {
    let expectation = self.expectation(description: "Sends stream data")

    let expectedData = Data([0x0, 0x1, 0x2])
    MockEnvoyHTTPStream.onData = { data, closeStream in
      XCTAssertEqual(expectedData, data)
      XCTAssertFalse(closeStream)
      expectation.fulfill()
    }

    let request = RequestBuilder(
      method: .post, scheme: "https", authority: "www.envoyproxy.io", path: "/test")
      .build()
    let emitter = self.envoy.start(request, handler: ResponseHandler())
    emitter.sendData(expectedData)
    self.wait(for: [expectation], timeout: 0.1)
  }

  func testClosingStreamWithDataSendsEmptyDataToTheUnderlyingStream() {
    let expectation = self.expectation(description: "Sends stream data")

    MockEnvoyHTTPStream.onTrailers = { _ in XCTFail("Should not call onTrailers") }
    MockEnvoyHTTPStream.onData = { data, closeStream in
      XCTAssertTrue(data.isEmpty)
      XCTAssertTrue(closeStream)
      expectation.fulfill()
    }

    let request = RequestBuilder(
      method: .post, scheme: "https", authority: "www.envoyproxy.io", path: "/test")
      .build()
    let emitter = self.envoy.start(request, handler: ResponseHandler())
    emitter.close(data: Data())
    self.wait(for: [expectation], timeout: 0.1)
  }

  func testClosingStreamWithTrailersSendsTrailersToTheUnderlyingStream() {
    let expectation = self.expectation(description: "Sends stream trailers")

    let expectedTrailers = ["key_1": ["value_a"]]
    MockEnvoyHTTPStream.onData = { _, _ in XCTFail("Should not call onData") }
    MockEnvoyHTTPStream.onTrailers = { trailers in
      XCTAssertEqual(expectedTrailers, trailers)
      expectation.fulfill()
    }

    let request = RequestBuilder(
      method: .post, scheme: "https", authority: "www.envoyproxy.io", path: "/test")
      .build()
    let emitter = self.envoy.start(request, handler: ResponseHandler())
    emitter.close(trailers: expectedTrailers)
    self.wait(for: [expectation], timeout: 0.1)
  }

  // MARK: - Unary

  func testSendingUnaryHeadersOnlyRequestClosesStreamWithHeaders() {
    let expectation = self.expectation(description: "Sends unary headers")
    let expectedHeaders = [
      "key_1": ["value_a"],
      ":method": ["POST"],
      ":scheme": ["https"],
      ":authority": ["www.envoyproxy.io"],
      ":path": ["/test"],
    ]

    MockEnvoyHTTPStream.onData = { _, _ in XCTFail("Should not call onData") }
    MockEnvoyHTTPStream.onTrailers = { _ in XCTFail("Should not call onTrailers") }
    MockEnvoyHTTPStream.onHeaders = { headers, closeStream in
      XCTAssertEqual(expectedHeaders, headers)
      XCTAssertTrue(closeStream)
      expectation.fulfill()
    }

    let request = RequestBuilder(
      method: .post, scheme: "https", authority: "www.envoyproxy.io", path: "/test")
      .addHeader(name: "key_1", value: "value_a")
      .build()
    self.envoy.send(request, body: nil, trailers: nil, handler: ResponseHandler())
    self.wait(for: [expectation], timeout: 0.1)
  }

  func testSendingUnaryHeadersAndDataWithNoTrailersClosesStreamWithData() {
    let headersExpectation = self.expectation(description: "Sends unary headers")
    let dataExpectation = self.expectation(description: "Sends unary data")

    let expectedData = Data([0x0, 0x1, 0x2])
    MockEnvoyHTTPStream.onTrailers = { _ in XCTFail("Should not call onTrailers") }
    MockEnvoyHTTPStream.onHeaders = { _, closeStream in
      XCTAssertFalse(closeStream)
      headersExpectation.fulfill()
    }

    MockEnvoyHTTPStream.onData = { data, closeStream in
      XCTAssertEqual(expectedData, data)
      XCTAssertTrue(closeStream)
      dataExpectation.fulfill()
    }

    let request = RequestBuilder(
      method: .post, scheme: "https", authority: "www.envoyproxy.io", path: "/test")
      .build()
    self.envoy.send(request, body: expectedData, trailers: nil, handler: ResponseHandler())
    self.wait(for: [headersExpectation, dataExpectation],
              timeout: 0.1, enforceOrder: true)
  }

  func testSendingUnaryTrailersClosesStreamWithTrailers() {
    let headersExpectation = self.expectation(description: "Sends unary headers")
    let dataExpectation = self.expectation(description: "Sends unary data")
    let trailersExpectation = self.expectation(description: "Sends unary trailers")

    let expectedData = Data([0x0, 0x1, 0x2])
    let expectedTrailers = ["key_1": ["value_a"]]
    MockEnvoyHTTPStream.onHeaders = { _, closeStream in
      XCTAssertFalse(closeStream)
      headersExpectation.fulfill()
    }

    MockEnvoyHTTPStream.onData = { data, closeStream in
      XCTAssertEqual(expectedData, data)
      XCTAssertFalse(closeStream)
      dataExpectation.fulfill()
    }

    MockEnvoyHTTPStream.onTrailers = { trailers in
      XCTAssertEqual(expectedTrailers, trailers)
      trailersExpectation.fulfill()
    }

    let request = RequestBuilder(
      method: .post, scheme: "https", authority: "www.envoyproxy.io", path: "/test")
      .build()
    self.envoy.send(request, body: expectedData,
                    trailers: expectedTrailers, handler: ResponseHandler())
    self.wait(for: [headersExpectation, dataExpectation, trailersExpectation],
              timeout: 0.1, enforceOrder: true)
  }
}

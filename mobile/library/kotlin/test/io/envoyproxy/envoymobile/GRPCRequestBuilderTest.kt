package io.envoyproxy.envoymobile

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class GRPCRequestBuilderTest {

  @Test
  fun `using https will pass https as scheme`() {
    val request = GRPCRequestBuilder("/pb.api.v1.Foo/GetBar", "foo.bar.com", true)
        .build()

    assertThat(request.scheme).isEqualTo("https")
  }

  @Test
  fun `not using https will pass http as scheme`() {
    val request = GRPCRequestBuilder("/pb.api.v1.Foo/GetBar", "foo.bar.com", false)
        .build()

    assertThat(request.scheme).isEqualTo("http")
  }

  @Test
  fun `application gprc is set as content-type header`() {
    val request = GRPCRequestBuilder("/pb.api.v1.Foo/GetBar", "foo.bar.com", false)
        .build()
    assertThat(request.headers["content-type"]).containsExactly("application/grpc")
  }

  @Test
  fun `POST is used as method`() {
    val request = GRPCRequestBuilder("/pb.api.v1.Foo/GetBar", "foo.bar.com", false)
        .build()
    assertThat(request.method).isEqualTo(RequestMethod.POST)
  }

  @Test
  fun `timeout is set as grpc-timeout header`() {
    val request = GRPCRequestBuilder("/pb.api.v1.Foo/GetBar", "foo.bar.com", false)
        .addTimeoutMS(200)
        .build()

    assertThat(request.headers["grpc-timeout"]).containsExactly("200m")
  }

  @Test
  fun `grpc-timeout header is not present when no timeout is set`() {
    val request = GRPCRequestBuilder("/pb.api.v1.Foo/GetBar", "foo.bar.com", false)
        .build()

    assertThat(request.headers.containsKey("grpc-timeout")).isFalse()
  }
}

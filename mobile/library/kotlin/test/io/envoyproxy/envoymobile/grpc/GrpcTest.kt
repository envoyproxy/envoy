package io.envoyproxy.envoymobile.grpc

import io.grpc.ManagedChannelBuilder
import io.grpc.ServerBuilder
import org.assertj.core.api.Assertions.assertThat
import org.junit.After
import org.junit.Before
import org.junit.Test
import protos.test.EchoServiceGrpc
import protos.test.EchoServiceTest

class GrpcTest {

  private val server = ServerBuilder.forPort(1234)
      .addService(EnvoyMobileEchoingTestServer())
      .build()

  @Before
  fun setup() {
    server.start()
  }

  @After
  fun teardown() {
    server.shutdown()
  }

  @Test
  fun `sanity grpc test`() {
    val channel = ManagedChannelBuilder.forAddress("localhost", 1234)
        .usePlaintext()
        .build()

    val response = EchoServiceGrpc.newBlockingStub(channel).unary(EchoServiceTest.EchoServiceRequest.newBuilder().setStr("hello_world").build())

    assertThat(response.str).isEqualTo("hello_world")
  }
}

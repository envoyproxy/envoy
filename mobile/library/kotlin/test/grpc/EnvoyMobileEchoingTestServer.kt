package io.envoyproxy.envoymobile.grpc

import io.grpc.stub.StreamObserver
import protos.test.EchoServiceGrpc
import protos.test.EchoServiceTest


class EnvoyMobileEchoingTestServer : EchoServiceGrpc.EchoServiceImplBase() {

  override fun unary(request: EchoServiceTest.EchoServiceRequest?, responseObserver: StreamObserver<EchoServiceTest.EchoServiceResponse>?) {
    responseObserver!!.onNext(EchoServiceTest.EchoServiceResponse.newBuilder().setStr(request!!.str).build())
    responseObserver.onCompleted()
  }

  override fun stream(responseObserver: StreamObserver<EchoServiceTest.EchoServiceResponse>?): StreamObserver<EchoServiceTest.EchoServiceRequest> {
    return object : StreamObserver<EchoServiceTest.EchoServiceRequest> {
      override fun onNext(value: EchoServiceTest.EchoServiceRequest?) {
        responseObserver!!.onNext(EchoServiceTest.EchoServiceResponse.newBuilder().setStr(value!!.str).build())
      }

      override fun onError(t: Throwable?) {
        responseObserver!!.onError(t)
      }

      override fun onCompleted() {
        responseObserver!!.onCompleted()
      }

    }
  }
}

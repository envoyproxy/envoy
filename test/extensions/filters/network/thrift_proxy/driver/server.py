#!/usr/bin/env python

import argparse
import logging
import sys

from generated.example import Example
from generated.example.ttypes import (
    Result, TheWorks, AppException
)

from thrift import Thrift, TMultiplexedProcessor
from thrift.protocol import TBinaryProtocol, TCompactProtocol, TJSONProtocol
from thrift.server import TServer
from thrift.transport import TSocket
from thrift.transport import TTransport
from fbthrift import THeaderTransport
from finagle import TFinagleServerProcessor, TFinagleServerProtocol


class SuccessHandler:
    def ping(self):
        print("server: ping()")

    def poke(self):
        print("server: poke()")

    def add(self, a, b):
        result = a + b
        print("server: add({0}, {1}) = {2}".format(a, b, result))
        return result

    def execute(self, param):
        print("server: execute({0})".format(param))
        if "all" in param.return_fields:
            return Result(param.the_works)
        elif "none" in param.return_fields:
            return Result(TheWorks())
        the_works = TheWorks()
        for field, value in vars(param.the_works).items():
            if field in param.return_fields:
                setattr(the_works, field, value)
        return Result(the_works)


class IDLExceptionHandler:
    def ping(self):
        print("server: ping()")

    def poke(self):
        print("server: poke()")

    def add(self, a, b):
        result = a + b
        print("server: add({0}, {1}) = {2}".format(a, b, result))
        return result

    def execute(self, param):
        print("server: app error: execute failed")
        raise AppException("execute failed")


class ExceptionHandler:
    def ping(self):
        print("server: ping failure")
        raise Thrift.TApplicationException(
            type=Thrift.TApplicationException.INTERNAL_ERROR,
            message="for ping",
        )

    def poke(self):
        print("server: poke failure")
        raise Thrift.TApplicationException(
            type=Thrift.TApplicationException.INTERNAL_ERROR,
            message="for poke",
        )

    def add(self, a, b):
        print("server: add failure")
        raise Thrift.TApplicationException(
            type=Thrift.TApplicationException.INTERNAL_ERROR,
            message="for add",
        )

    def execute(self, param):
        print("server: execute failure")
        raise Thrift.TApplicationException(
            type=Thrift.TApplicationException.INTERNAL_ERROR,
            message="for execute",
        )


def main(cfg):
    if cfg.unix:
        if cfg.addr == "":
            sys.exit("invalid listener unix domain socket: {}".format(cfg.addr))
    else:
        try:
            (host, port) = cfg.addr.rsplit(":", 1)
            port = int(port)
        except ValueError:
            sys.exit("invalid listener address: {}".format(cfg.addr))

    if cfg.response == "success":
        handler = SuccessHandler()
    elif cfg.response == "idl-exception":
        handler = IDLExceptionHandler()
    elif cfg.response == "exception":
        # squelch traceback for the exception we throw
        logging.getLogger().setLevel(logging.CRITICAL)
        handler = ExceptionHandler()
    else:
        sys.exit("unknown server response mode {0}".format(cfg.response))

    processor = Example.Processor(handler)
    if cfg.service is not None:
        # wrap processor with multiplexor
        multi = TMultiplexedProcessor.TMultiplexedProcessor()
        multi.registerProcessor(cfg.service, processor)
        processor = multi

    if cfg.protocol == "finagle":
        # wrap processor with finagle request/response header handler
        processor = TFinagleServerProcessor.TFinagleServerProcessor(processor)

    if cfg.unix:
        transport = TSocket.TServerSocket(unix_socket=cfg.addr)
    else:
        transport = TSocket.TServerSocket(host=host, port=port)

    if cfg.transport == "framed":
        transport_factory = TTransport.TFramedTransportFactory()
    elif cfg.transport == "unframed":
        transport_factory = TTransport.TBufferedTransportFactory()
    elif cfg.transport == "header":
        transport_factory = THeaderTransport.THeaderTransportFactory()
    else:
        sys.exit("unknown transport {0}".format(cfg.transport))

    if cfg.protocol == "binary":
        protocol_factory = TBinaryProtocol.TBinaryProtocolFactory()
    elif cfg.protocol == "compact":
        protocol_factory = TCompactProtocol.TCompactProtocolFactory()
    elif cfg.protocol == "json":
        protocol_factory = TJSONProtocol.TJSONProtocolFactory()
    elif cfg.protocol == "finagle":
        protocol_factory = TFinagleServerProtocol.TFinagleServerProtocolFactory()
    else:
        sys.exit("unknown protocol {0}".format(cfg.protocol))

    print("Thrift Server listening on {0} for {1} {2} requests".format(
        cfg.addr, cfg.transport, cfg.protocol))
    if cfg.service is not None:
        print("Thrift Server service name {0}".format(cfg.service))
    if cfg.response == "idl-exception":
        print("Thrift Server will throw IDL exceptions when defined")
    elif cfg.response == "exception":
        print("Thrift Server will throw Thrift exceptions for all messages")

    server = TServer.TThreadedServer(processor, transport, transport_factory, protocol_factory)
    try:
        server.serve()
    except KeyboardInterrupt:
        print


if __name__ == "__main__":
    logging.basicConfig()
    parser = argparse.ArgumentParser(description="Thrift server to match client.py.")
    parser.add_argument(
        "-a",
        "--addr",
        metavar="ADDR",
        dest="addr",
        default=":0",
        help="Listener address for server in the form host:port. The host is optional. If --unix" +
             " is set, the address is the socket name.",
    )
    parser.add_argument(
        "-m",
        "--multiplex",
        metavar="SERVICE",
        dest="service",
        help="Enable service multiplexing and set the service name.",
    )
    parser.add_argument(
        "-p",
        "--protocol",
        help="Selects a protocol.",
        dest="protocol",
        default="binary",
        choices=["binary", "compact", "json", "finagle"],
    )
    parser.add_argument(
        "-r",
        "--response",
        dest="response",
        default="success",
        choices=["success", "idl-exception", "exception"],
        help="Controls how the server responds to requests",
    )
    parser.add_argument(
        "-t",
        "--transport",
        help="Selects a transport.",
        dest="transport",
        default="framed",
        choices=["framed", "unframed", "header"],
    )
    parser.add_argument(
        "-u",
        "--unix",
        dest="unix",
        action="store_true",
    )
    cfg = parser.parse_args()

    try:
        main(cfg)
    except Thrift.TException as tx:
        sys.exit("Thrift exception: {0}".format(tx.message))

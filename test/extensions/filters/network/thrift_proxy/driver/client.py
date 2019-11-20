#!/usr/bin/env python

import argparse
import io
import sys

from generated.example import Example
from generated.example.ttypes import (Param, TheWorks, AppException)

from thrift import Thrift
from thrift.protocol import (TBinaryProtocol, TCompactProtocol, TJSONProtocol, TMultiplexedProtocol)
from thrift.transport import TSocket
from thrift.transport import TTransport
from fbthrift import THeaderTransport
from twitter.common.rpc.finagle.protocol import TFinagleProtocol


class TRecordingTransport(TTransport.TTransportBase):

  def __init__(self, underlying, writehandle, readhandle):
    self._underlying = underlying
    self._whandle = writehandle
    self._rhandle = readhandle

  def isOpen(self):
    return self._underlying.isOpen()

  def open(self):
    if not self._underlying.isOpen():
      self._underlying.open()

  def close(self):
    self._underlying.close()
    self._whandle.close()
    self._rhandle.close()

  def read(self, sz):
    buf = self._underlying.read(sz)
    if len(buf) != 0:
      self._rhandle.write(buf)
    return buf

  def write(self, buf):
    if len(buf) != 0:
      self._whandle.write(buf)
    self._underlying.write(buf)

  def flush(self):
    self._underlying.flush()
    self._whandle.flush()
    self._rhandle.flush()


def main(cfg, reqhandle, resphandle):
  if cfg.unix:
    if cfg.addr == "":
      sys.exit("invalid unix domain socket: {}".format(cfg.addr))
    socket = TSocket.TSocket(unix_socket=cfg.addr)
  else:
    try:
      (host, port) = cfg.addr.rsplit(":", 1)
      if host == "":
        host = "localhost"
      socket = TSocket.TSocket(host=host, port=int(port))
    except ValueError:
      sys.exit("invalid address: {}".format(cfg.addr))

  transport = TRecordingTransport(socket, reqhandle, resphandle)

  if cfg.transport == "framed":
    transport = TTransport.TFramedTransport(transport)
  elif cfg.transport == "unframed":
    transport = TTransport.TBufferedTransport(transport)
  elif cfg.transport == "header":
    transport = THeaderTransport.THeaderTransport(
        transport,
        client_type=THeaderTransport.CLIENT_TYPE.HEADER,
    )

    if cfg.headers is not None:
      pairs = cfg.headers.split(",")
      for p in pairs:
        key, value = p.split("=")
        transport.set_header(key, value)

    if cfg.protocol == "binary":
      transport.set_protocol_id(THeaderTransport.T_BINARY_PROTOCOL)
    elif cfg.protocol == "compact":
      transport.set_protocol_id(THeaderTransport.T_COMPACT_PROTOCOL)
    else:
      sys.exit("header transport cannot be used with protocol {0}".format(cfg.protocol))
  else:
    sys.exit("unknown transport {0}".format(cfg.transport))

  transport.open()

  if cfg.protocol == "binary":
    protocol = TBinaryProtocol.TBinaryProtocol(transport)
  elif cfg.protocol == "compact":
    protocol = TCompactProtocol.TCompactProtocol(transport)
  elif cfg.protocol == "json":
    protocol = TJSONProtocol.TJSONProtocol(transport)
  elif cfg.protocol == "finagle":
    protocol = TFinagleProtocol(transport, client_id="thrift-playground")
  else:
    sys.exit("unknown protocol {0}".format(cfg.protocol))

  if cfg.service is not None:
    protocol = TMultiplexedProtocol.TMultiplexedProtocol(protocol, cfg.service)

  client = Example.Client(protocol)

  try:
    if cfg.method == "ping":
      client.ping()
      print("client: pinged")
    elif cfg.method == "poke":
      client.poke()
      print("client: poked")
    elif cfg.method == "add":
      if len(cfg.params) != 2:
        sys.exit("add takes 2 arguments, got: {0}".format(cfg.params))

      a = int(cfg.params[0])
      b = int(cfg.params[1])
      v = client.add(a, b)
      print("client: added {0} + {1} = {2}".format(a, b, v))
    elif cfg.method == "execute":
      param = Param(return_fields=cfg.params,
                    the_works=TheWorks(
                        field_1=True,
                        field_2=0x7f,
                        field_3=0x7fff,
                        field_4=0x7fffffff,
                        field_5=0x7fffffffffffffff,
                        field_6=-1.5,
                        field_7=u"string is UTF-8: \U0001f60e",
                        field_8=b"binary is bytes: \x80\x7f\x00\x01",
                        field_9={
                            1: "one",
                            2: "two",
                            3: "three"
                        },
                        field_10=[1, 2, 4, 8],
                        field_11=set(["a", "b", "c"]),
                        field_12=False,
                    ))

      try:
        result = client.execute(param)
        print("client: executed {0}: {1}".format(param, result))
      except AppException as e:
        print("client: execute failed with IDL Exception: {0}".format(e.why))
    else:
      sys.exit("unknown method {0}".format(cfg.method))
  except Thrift.TApplicationException as e:
    print("client exception: {0}: {1}".format(e.type, e.message))

  if cfg.request is None:
    req = "".join(["%02X " % ord(x) for x in reqhandle.getvalue()]).strip()
    print("request: {}".format(req))
  if cfg.response is None:
    resp = "".join(["%02X " % ord(x) for x in resphandle.getvalue()]).strip()
    print("response: {}".format(resp))

  transport.close()


if __name__ == "__main__":
  parser = argparse.ArgumentParser(description="Thrift client tool.",)
  parser.add_argument(
      "method",
      metavar="METHOD",
      help="Name of the service method to invoke.",
  )
  parser.add_argument(
      "params",
      metavar="PARAMS",
      nargs="*",
      help="Method parameters",
  )
  parser.add_argument(
      "-a",
      "--addr",
      metavar="ADDR",
      dest="addr",
      required=True,
      help="Target address for requests in the form host:port. The host is optional. If --unix" +
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
      dest="protocol",
      default="binary",
      choices=["binary", "compact", "json", "finagle"],
      help="selects a protocol.",
  )
  parser.add_argument(
      "--request",
      metavar="FILE",
      dest="request",
      help="Writes the Thrift request to a file.",
  )
  parser.add_argument(
      "--response",
      metavar="FILE",
      dest="response",
      help="Writes the Thrift response to a file.",
  )
  parser.add_argument(
      "-t",
      "--transport",
      dest="transport",
      default="framed",
      choices=["framed", "unframed", "header"],
      help="selects a transport.",
  )
  parser.add_argument(
      "-u",
      "--unix",
      dest="unix",
      action="store_true",
  )
  parser.add_argument(
      "--headers",
      dest="headers",
      metavar="KEY=VALUE[,KEY=VALUE]",
      help="list of comma-delimited, key value pairs to include as transport headers.",
  )

  cfg = parser.parse_args()

  reqhandle = io.BytesIO()
  resphandle = io.BytesIO()
  if cfg.request is not None:
    try:
      reqhandle = io.open(cfg.request, "wb")
    except IOError as e:
      sys.exit("I/O error({0}): {1}".format(e.errno, e.strerror))
  if cfg.response is not None:
    try:
      resphandle = io.open(cfg.response, "wb")
    except IOError as e:
      sys.exit("I/O error({0}): {1}".format(e.errno, e.strerror))
  try:
    main(cfg, reqhandle, resphandle)
  except Thrift.TException as tx:
    sys.exit("Unhandled Thrift Exception: {0}".format(tx.message))

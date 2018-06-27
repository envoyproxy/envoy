import logging

from thrift.Thrift import TProcessor, TMessageType, TException
from thrift.protocol import TProtocolDecorator
from gen.twitter.finagle.thrift.ttypes import (ConnectionOptions, UpgradeReply)

# Matches twitter/common/rpc/finagle/protocol.py
UPGRADE_METHOD = "__can__finagle__trace__v3__"

# Twitter's TFinagleProcessor only works for the client side of an RPC.
class TFinagleServerProcessor(TProcessor):
    def __init__(self, underlying):
        self._underlying = underlying

    def process(self, iprot, oprot):
        try:
            if iprot.upgraded() is not None:
                return self._underlying.process(iprot, oprot)
        except AttributeError as e:
            logging.exception("underlying protocol object is not a TFinagleServerProtocol", e)
            return self._underlying.process(iprot, oprot)

        (name, ttype, seqid) = iprot.readMessageBegin()
        if ttype != TMessageType.CALL and ttype != TMessageType.ONEWAY:
            raise TException("TFinagle protocol only supports CALL & ONEWAY")

        # Check if this is an upgrade request.
        if name == UPGRADE_METHOD:
            connection_options = ConnectionOptions()
            connection_options.read(iprot)
            iprot.readMessageEnd()

            oprot.writeMessageBegin(UPGRADE_METHOD, TMessageType.REPLY, seqid)
            upgrade_reply = UpgradeReply()
            upgrade_reply.write(oprot)
            oprot.writeMessageEnd()
            oprot.trans.flush()

            iprot.set_upgraded(True)
            oprot.set_upgraded(True)
            return True

        # Not upgraded. Replay the message begin to the underlying processor.
        iprot.set_upgraded(False)
        oprot.set_upgraded(False)
        msg = (name, ttype, seqid)
        return self._underlying.process(StoredMessageProtocol(iprot, msg), oprot)


class StoredMessageProtocol(TProtocolDecorator.TProtocolDecorator):
    def __init__(self, protocol, messageBegin):
        TProtocolDecorator.TProtocolDecorator.__init__(self, protocol)
        self.messageBegin = messageBegin

    def readMessageBegin(self):
        return self.messageBegin

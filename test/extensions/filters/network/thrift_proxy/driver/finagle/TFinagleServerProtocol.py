from thrift.protocol import TBinaryProtocol
from gen.twitter.finagle.thrift.ttypes import (RequestHeader, ResponseHeader)


class TFinagleServerProtocolFactory(object):
    def getProtocol(self, trans):
        return TFinagleServerProtocol(trans)


class TFinagleServerProtocol(TBinaryProtocol.TBinaryProtocol):
    def __init__(self, *args, **kw):
        self._last_request = None
        self._upgraded = None
        TBinaryProtocol.TBinaryProtocol.__init__(self, *args, **kw)

    def upgraded(self):
        return self._upgraded

    def set_upgraded(self, upgraded):
        self._upgraded = upgraded

    def writeMessageBegin(self, *args, **kwargs):
        if self._upgraded:
            header = ResponseHeader()  # .. TODO set some fields
            header.write(self)
        return TBinaryProtocol.TBinaryProtocol.writeMessageBegin(self, *args, **kwargs)

    def readMessageBegin(self, *args, **kwargs):
        if self._upgraded:
            header = RequestHeader()
            header.read(self)
            self._last_request = header
        return TBinaryProtocol.TBinaryProtocol.readMessageBegin(self, *args, **kwargs)

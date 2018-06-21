#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements. See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership. The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License. You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the License for the
# specific language governing permissions and limitations
# under the License.
#

# INFO:(zuercher):  Adapted from
# https://github.com/facebook/fbthrift/blob/b090870/thrift/lib/py/transport/THeaderTransport.py

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import sys
if sys.version_info[0] >= 3:
    from http import server
    BaseHTTPServer = server
    xrange = range
    from io import BytesIO as StringIO
    PY3 = True
else:
    import BaseHTTPServer
    from cStringIO import StringIO
    PY3 = False

from struct import pack, unpack
import zlib

from thrift.Thrift import TApplicationException
from thrift.transport.TTransport import TTransportException, TTransportBase, CReadableTransport

# INFO:(zuercher): Instead of importing these constants from TBinaryProtocol and TCompactProtocol
BINARY_PROTO_ID = 0x80
COMPACT_PROTO_ID = 0x82


# INFO:(zuercher): Copied from:
# https://github.com/facebook/fbthrift/blob/b090870/thrift/lib/py/protocol/TCompactProtocol.py
def getVarint(n):
    out = []
    while True:
        if n & ~0x7f == 0:
            out.append(n)
            break
        else:
            out.append((n & 0xff) | 0x80)
            n = n >> 7
    if sys.version_info[0] >= 3:
        return bytes(out)
    else:
        return b''.join(map(chr, out))


# INFO:(zuercher): Copied from
# https://github.com/facebook/fbthrift/blob/b090870/thrift/lib/py/protocol/TCompactProtocol.py
def readVarint(trans):
    result = 0
    shift = 0
    while True:
        x = trans.read(1)
        byte = ord(x)
        result |= (byte & 0x7f) << shift
        if byte >> 7 == 0:
            return result
        shift += 7


# Import the snappy module if it is available
try:
    import snappy
except ImportError:
    # If snappy is not available, don't fail immediately.
    # Only raise an error if we actually ever need to perform snappy
    # compression.
    class DummySnappy(object):
        def compress(self, buf):
            raise TTransportException(TTransportException.INVALID_TRANSFORM,
                                      'snappy module not available')

        def decompress(self, buf):
            raise TTransportException(TTransportException.INVALID_TRANSFORM,
                                      'snappy module not available')
    snappy = DummySnappy()  # type: ignore


# Definitions from THeader.h


class CLIENT_TYPE:
    HEADER = 0
    FRAMED_DEPRECATED = 1
    UNFRAMED_DEPRECATED = 2
    HTTP_SERVER = 3
    HTTP_CLIENT = 4
    FRAMED_COMPACT = 5
    HEADER_SASL = 6
    HTTP_GET = 7
    UNKNOWN = 8
    UNFRAMED_COMPACT_DEPRECATED = 9


class HEADER_FLAG:
    SUPPORT_OUT_OF_ORDER = 0x01
    DUPLEX_REVERSE = 0x08
    SASL = 0x10


class TRANSFORM:
    NONE = 0x00
    ZLIB = 0x01
    HMAC = 0x02
    SNAPPY = 0x03
    QLZ = 0x04
    ZSTD = 0x05


class INFO:
    NORMAL = 1
    PERSISTENT = 2


T_BINARY_PROTOCOL = 0
T_COMPACT_PROTOCOL = 2
HEADER_MAGIC = 0x0FFF0000
PACKED_HEADER_MAGIC = pack(b'!H', HEADER_MAGIC >> 16)
HEADER_MASK = 0xFFFF0000
FLAGS_MASK = 0x0000FFFF
HTTP_SERVER_MAGIC = 0x504F5354  # POST
HTTP_CLIENT_MAGIC = 0x48545450  # HTTP
HTTP_GET_CLIENT_MAGIC = 0x47455420  # GET
HTTP_HEAD_CLIENT_MAGIC = 0x48454144  # HEAD
BIG_FRAME_MAGIC = 0x42494746  # BIGF
MAX_FRAME_SIZE = 0x3FFFFFFF
MAX_BIG_FRAME_SIZE = 2 ** 61 - 1


class THeaderTransport(TTransportBase, CReadableTransport):
    """Transport that sends headers.  Also understands framed/unframed/HTTP
    transports and will do the right thing"""

    __max_frame_size = MAX_FRAME_SIZE

    # Defaults to current user, but there is also a setter below.
    __identity = None
    IDENTITY_HEADER = "identity"
    ID_VERSION_HEADER = "id_version"
    ID_VERSION = "1"

    def __init__(self, trans, client_types=None, client_type=None):
        self.__trans = trans
        self.__rbuf = StringIO()
        self.__rbuf_frame = False
        self.__wbuf = StringIO()
        self.seq_id = 0
        self.__flags = 0
        self.__read_transforms = []
        self.__write_transforms = []
        self.__supported_client_types = set(client_types or
                                            (CLIENT_TYPE.HEADER,))
        self.__proto_id = T_COMPACT_PROTOCOL  # default to compact like c++
        self.__client_type = client_type or CLIENT_TYPE.HEADER
        self.__read_headers = {}
        self.__read_persistent_headers = {}
        self.__write_headers = {}
        self.__write_persistent_headers = {}

        self.__supported_client_types.add(self.__client_type)

        # If we support unframed binary / framed binary also support compact
        if CLIENT_TYPE.UNFRAMED_DEPRECATED in self.__supported_client_types:
            self.__supported_client_types.add(
                CLIENT_TYPE.UNFRAMED_COMPACT_DEPRECATED)
        if CLIENT_TYPE.FRAMED_DEPRECATED in self.__supported_client_types:
            self.__supported_client_types.add(
                CLIENT_TYPE.FRAMED_COMPACT)

    def set_header_flag(self, flag):
        self.__flags |= flag

    def clear_header_flag(self, flag):
        self.__flags &= ~ flag

    def header_flags(self):
        return self.__flags

    def set_max_frame_size(self, size):
        if size > MAX_BIG_FRAME_SIZE:
            raise TTransportException(TTransportException.INVALID_FRAME_SIZE,
                                      "Cannot set max frame size > %s" %
                                      MAX_BIG_FRAME_SIZE)
        if size > MAX_FRAME_SIZE and self.__client_type != CLIENT_TYPE.HEADER:
            raise TTransportException(
                TTransportException.INVALID_FRAME_SIZE,
                "Cannot set max frame size > %s for clients other than HEADER"
                % MAX_FRAME_SIZE)
        self.__max_frame_size = size

    def get_peer_identity(self):
        if self.IDENTITY_HEADER in self.__read_headers:
            if self.__read_headers[self.ID_VERSION_HEADER] == self.ID_VERSION:
                return self.__read_headers[self.IDENTITY_HEADER]
        return None

    def set_identity(self, identity):
        self.__identity = identity

    def get_protocol_id(self):
        return self.__proto_id

    def set_protocol_id(self, proto_id):
        self.__proto_id = proto_id

    def set_header(self, str_key, str_value):
        self.__write_headers[str_key] = str_value

    def get_write_headers(self):
        return self.__write_headers

    def get_headers(self):
        return self.__read_headers

    def clear_headers(self):
        self.__write_headers.clear()

    def set_persistent_header(self, str_key, str_value):
        self.__write_persistent_headers[str_key] = str_value

    def get_write_persistent_headers(self):
        return self.__write_persistent_headers

    def clear_persistent_headers(self):
        self.__write_persistent_headers.clear()

    def add_transform(self, trans_id):
        self.__write_transforms.append(trans_id)

    def _reset_protocol(self):
        # HTTP calls that are one way need to flush here.
        if self.__client_type == CLIENT_TYPE.HTTP_SERVER:
            self.flush()
        # set to anything except unframed
        self.__client_type = CLIENT_TYPE.UNKNOWN
        # Read header bytes to check which protocol to decode
        self.readFrame(0)

    def getTransport(self):
        return self.__trans

    def isOpen(self):
        return self.getTransport().isOpen()

    def open(self):
        return self.getTransport().open()

    def close(self):
        return self.getTransport().close()

    def read(self, sz):
        ret = self.__rbuf.read(sz)
        if len(ret) == sz:
            return ret

        if self.__client_type in (CLIENT_TYPE.UNFRAMED_DEPRECATED,
                                  CLIENT_TYPE.UNFRAMED_COMPACT_DEPRECATED):
            return ret + self.getTransport().readAll(sz - len(ret))

        self.readFrame(sz - len(ret))
        return ret + self.__rbuf.read(sz - len(ret))

    readAll = read  # TTransportBase.readAll does a needless copy here.

    def readFrame(self, req_sz):
        self.__rbuf_frame = True
        word1 = self.getTransport().readAll(4)
        sz = unpack('!I', word1)[0]
        proto_id = word1[0] if PY3 else ord(word1[0])
        if proto_id == BINARY_PROTO_ID:
            # unframed
            self.__client_type = CLIENT_TYPE.UNFRAMED_DEPRECATED
            self.__proto_id = T_BINARY_PROTOCOL
            if req_sz <= 4:  # check for reads < 0.
                self.__rbuf = StringIO(word1)
            else:
                self.__rbuf = StringIO(word1 + self.getTransport().read(
                    req_sz - 4))
        elif proto_id == COMPACT_PROTO_ID:
            self.__client_type = CLIENT_TYPE.UNFRAMED_COMPACT_DEPRECATED
            self.__proto_id = T_COMPACT_PROTOCOL
            if req_sz <= 4:  # check for reads < 0.
                self.__rbuf = StringIO(word1)
            else:
                self.__rbuf = StringIO(word1 + self.getTransport().read(
                    req_sz - 4))
        elif sz == HTTP_SERVER_MAGIC:
            self.__client_type = CLIENT_TYPE.HTTP_SERVER
            mf = self.getTransport().handle.makefile('rb', -1)

            self.handler = RequestHandler(mf,
                                          'client_address:port', '')
            self.header = self.handler.wfile
            self.__rbuf = StringIO(self.handler.data)
        else:
            if sz == BIG_FRAME_MAGIC:
                sz = unpack('!Q', self.getTransport().readAll(8))[0]
            # could be header format or framed.  Check next two bytes.
            magic = self.getTransport().readAll(2)
            proto_id = magic[0] if PY3 else ord(magic[0])
            if proto_id == COMPACT_PROTO_ID:
                self.__client_type = CLIENT_TYPE.FRAMED_COMPACT
                self.__proto_id = T_COMPACT_PROTOCOL
                _frame_size_check(sz, self.__max_frame_size, header=False)
                self.__rbuf = StringIO(magic + self.getTransport().readAll(
                    sz - 2))
            elif proto_id == BINARY_PROTO_ID:
                self.__client_type = CLIENT_TYPE.FRAMED_DEPRECATED
                self.__proto_id = T_BINARY_PROTOCOL
                _frame_size_check(sz, self.__max_frame_size, header=False)
                self.__rbuf = StringIO(magic + self.getTransport().readAll(
                    sz - 2))
            elif magic == PACKED_HEADER_MAGIC:
                self.__client_type = CLIENT_TYPE.HEADER
                _frame_size_check(sz, self.__max_frame_size)
                # flags(2), seq_id(4), header_size(2)
                n_header_meta = self.getTransport().readAll(8)
                self.__flags, self.seq_id, header_size = unpack('!HIH',
                                                                n_header_meta)
                data = StringIO()
                data.write(magic)
                data.write(n_header_meta)
                data.write(self.getTransport().readAll(sz - 10))
                data.seek(10)
                self.read_header_format(sz - 10, header_size, data)
            else:
                self.__client_type = CLIENT_TYPE.UNKNOWN
                raise TTransportException(
                    TTransportException.INVALID_CLIENT_TYPE,
                    "Could not detect client transport type")

        if self.__client_type not in self.__supported_client_types:
            raise TTransportException(TTransportException.INVALID_CLIENT_TYPE,
                                      "Client type {} not supported on server"
                                      .format(self.__client_type))

    def read_header_format(self, sz, header_size, data):
        # clear out any previous transforms
        self.__read_transforms = []

        header_size = header_size * 4
        if header_size > sz:
            raise TTransportException(TTransportException.INVALID_FRAME_SIZE,
                                      "Header size is larger than frame")
        end_header = header_size + data.tell()

        self.__proto_id = readVarint(data)
        num_headers = readVarint(data)

        if self.__proto_id == 1 and self.__client_type != \
                CLIENT_TYPE.HTTP_SERVER:
            raise TTransportException(TTransportException.INVALID_CLIENT_TYPE,
                                      "Trying to recv JSON encoding over binary")

        # Read the headers.  Data for each header varies.
        for _ in range(0, num_headers):
            trans_id = readVarint(data)
            if trans_id == TRANSFORM.ZLIB:
                self.__read_transforms.insert(0, trans_id)
            elif trans_id == TRANSFORM.SNAPPY:
                self.__read_transforms.insert(0, trans_id)
            elif trans_id == TRANSFORM.HMAC:
                raise TApplicationException(
                    TApplicationException.INVALID_TRANSFORM,
                    "Hmac transform is no longer supported: %i" % trans_id)
            else:
                # TApplicationException will be sent back to client
                raise TApplicationException(
                    TApplicationException.INVALID_TRANSFORM,
                    "Unknown transform in client request: %i" % trans_id)

        # Clear out previous info headers.
        self.__read_headers.clear()

        # Read the info headers.
        while data.tell() < end_header:
            info_id = readVarint(data)
            if info_id == INFO.NORMAL:
                _read_info_headers(
                    data, end_header, self.__read_headers)
            elif info_id == INFO.PERSISTENT:
                _read_info_headers(
                    data, end_header, self.__read_persistent_headers)
            else:
                break  # Unknown header.  Stop info processing.

        if self.__read_persistent_headers:
            self.__read_headers.update(self.__read_persistent_headers)

        # Skip the rest of the header
        data.seek(end_header)

        payload = data.read(sz - header_size)

        # Read the data section.
        self.__rbuf = StringIO(self.untransform(payload))

    def write(self, buf):
        self.__wbuf.write(buf)

    def transform(self, buf):
        for trans_id in self.__write_transforms:
            if trans_id == TRANSFORM.ZLIB:
                buf = zlib.compress(buf)
            elif trans_id == TRANSFORM.SNAPPY:
                buf = snappy.compress(buf)
            else:
                raise TTransportException(TTransportException.INVALID_TRANSFORM,
                                          "Unknown transform during send")
        return buf

    def untransform(self, buf):
        for trans_id in self.__read_transforms:
            if trans_id == TRANSFORM.ZLIB:
                buf = zlib.decompress(buf)
            elif trans_id == TRANSFORM.SNAPPY:
                buf = snappy.decompress(buf)
            if trans_id not in self.__write_transforms:
                self.__write_transforms.append(trans_id)
        return buf

    def flush(self):
        self.flushImpl(False)

    def onewayFlush(self):
        self.flushImpl(True)

    def _flushHeaderMessage(self, buf, wout, wsz):
        """Write a message for CLIENT_TYPE.HEADER

        @param buf(StringIO): Buffer to write message to
        @param wout(str): Payload
        @param wsz(int): Payload length
        """
        transform_data = StringIO()
        # For now, all transforms don't require data.
        num_transforms = len(self.__write_transforms)
        for trans_id in self.__write_transforms:
            transform_data.write(getVarint(trans_id))

        # Add in special flags.
        if self.__identity:
            self.__write_headers[self.ID_VERSION_HEADER] = self.ID_VERSION
            self.__write_headers[self.IDENTITY_HEADER] = self.__identity

        info_data = StringIO()

        # Write persistent kv-headers
        _flush_info_headers(info_data,
                            self.get_write_persistent_headers(),
                            INFO.PERSISTENT)

        # Write non-persistent kv-headers
        _flush_info_headers(info_data,
                            self.__write_headers,
                            INFO.NORMAL)

        header_data = StringIO()
        header_data.write(getVarint(self.__proto_id))
        header_data.write(getVarint(num_transforms))

        header_size = transform_data.tell() + header_data.tell() + \
            info_data.tell()

        padding_size = 4 - (header_size % 4)
        header_size = header_size + padding_size

        # MAGIC(2) | FLAGS(2) + SEQ_ID(4) + HEADER_SIZE(2)
        wsz += header_size + 10
        if wsz > MAX_FRAME_SIZE:
            buf.write(pack("!I", BIG_FRAME_MAGIC))
            buf.write(pack("!Q", wsz))
        else:
            buf.write(pack("!I", wsz))
        buf.write(pack("!HH", HEADER_MAGIC >> 16, self.__flags))
        buf.write(pack("!I", self.seq_id))
        buf.write(pack("!H", header_size // 4))

        buf.write(header_data.getvalue())
        buf.write(transform_data.getvalue())
        buf.write(info_data.getvalue())

        # Pad out the header with 0x00
        for _ in range(0, padding_size, 1):
            buf.write(pack("!c", b'\0'))

        # Send data section
        buf.write(wout)

    def flushImpl(self, oneway):
        wout = self.__wbuf.getvalue()
        wout = self.transform(wout)
        wsz = len(wout)

        # reset wbuf before write/flush to preserve state on underlying failure
        self.__wbuf.seek(0)
        self.__wbuf.truncate()

        if self.__proto_id == 1 and self.__client_type != CLIENT_TYPE.HTTP_SERVER:
            raise TTransportException(TTransportException.INVALID_CLIENT_TYPE,
                                      "Trying to send JSON encoding over binary")

        buf = StringIO()
        if self.__client_type == CLIENT_TYPE.HEADER:
            self._flushHeaderMessage(buf, wout, wsz)
        elif self.__client_type in (CLIENT_TYPE.FRAMED_DEPRECATED,
                                    CLIENT_TYPE.FRAMED_COMPACT):
            buf.write(pack("!i", wsz))
            buf.write(wout)
        elif self.__client_type in (CLIENT_TYPE.UNFRAMED_DEPRECATED,
                                    CLIENT_TYPE.UNFRAMED_COMPACT_DEPRECATED):
            buf.write(wout)
        elif self.__client_type == CLIENT_TYPE.HTTP_SERVER:
            # Reset the client type if we sent something -
            # oneway calls via HTTP expect a status response otherwise
            buf.write(self.header.getvalue())
            buf.write(wout)
            self.__client_type == CLIENT_TYPE.HEADER
        elif self.__client_type == CLIENT_TYPE.UNKNOWN:
            raise TTransportException(TTransportException.INVALID_CLIENT_TYPE,
                                      "Unknown client type")

        # We don't include the framing bytes as part of the frame size check
        frame_size = buf.tell() - (4 if wsz < MAX_FRAME_SIZE else 12)
        _frame_size_check(frame_size,
                          self.__max_frame_size,
                          header=self.__client_type == CLIENT_TYPE.HEADER)
        self.getTransport().write(buf.getvalue())
        if oneway:
            self.getTransport().onewayFlush()
        else:
            self.getTransport().flush()

    # Implement the CReadableTransport interface.
    @property
    def cstringio_buf(self):
        if not self.__rbuf_frame:
            self.readFrame(0)
        return self.__rbuf

    def cstringio_refill(self, prefix, reqlen):
        # self.__rbuf will already be empty here because fastproto doesn't
        # ask for a refill until the previous buffer is empty.  Therefore,
        # we can start reading new frames immediately.

        # On unframed clients, there is a chance there is something left
        # in rbuf, and the read pointer is not advanced by fastproto
        # so seek to the end to be safe
        self.__rbuf.seek(0, 2)
        while len(prefix) < reqlen:
            prefix += self.read(reqlen)
        self.__rbuf = StringIO(prefix)
        return self.__rbuf


def _serialize_string(str_):
    if PY3 and not isinstance(str_, bytes):
        str_ = str_.encode()
    return getVarint(len(str_)) + str_


def _flush_info_headers(info_data, write_headers, type):
    if (len(write_headers) > 0):
        info_data.write(getVarint(type))
        info_data.write(getVarint(len(write_headers)))
        write_headers_iter = write_headers.items()
        for str_key, str_value in write_headers_iter:
            info_data.write(_serialize_string(str_key))
            info_data.write(_serialize_string(str_value))
        write_headers.clear()


def _read_string(bufio, buflimit):
    str_sz = readVarint(bufio)
    if str_sz + bufio.tell() > buflimit:
        raise TTransportException(TTransportException.INVALID_FRAME_SIZE,
                                  "String read too big")
    return bufio.read(str_sz)


def _read_info_headers(data, end_header, read_headers):
    num_keys = readVarint(data)
    for _ in xrange(num_keys):
        str_key = _read_string(data, end_header)
        str_value = _read_string(data, end_header)
        read_headers[str_key] = str_value


def _frame_size_check(sz, set_max_size, header=True):
    if sz > set_max_size or (not header and sz > MAX_FRAME_SIZE):
        raise TTransportException(
            TTransportException.INVALID_FRAME_SIZE,
            "%s transport frame was too large" % 'Header' if header else 'Framed'
        )


class RequestHandler(BaseHTTPServer.BaseHTTPRequestHandler):

    # Same as superclass function, but append 'POST' because we
    # stripped it in the calling function.  Would be nice if
    # we had an ungetch instead
    def handle_one_request(self):
        self.raw_requestline = self.rfile.readline()
        if not self.raw_requestline:
            self.close_connection = 1
            return
        self.raw_requestline = "POST" + self.raw_requestline
        if not self.parse_request():
            # An error code has been sent, just exit
            return
        mname = 'do_' + self.command
        if not hasattr(self, mname):
            self.send_error(501, "Unsupported method (%r)" % self.command)
            return
        method = getattr(self, mname)
        method()

    def setup(self):
        self.rfile = self.request
        self.wfile = StringIO()  # New output buffer

    def finish(self):
        if not self.rfile.closed:
            self.rfile.close()
        # leave wfile open for reading.

    def do_POST(self):
        if int(self.headers['Content-Length']) > 0:
            self.data = self.rfile.read(int(self.headers['Content-Length']))
        else:
            self.data = ""

        # Prepare a response header, to be sent later.
        self.send_response(200)
        self.send_header("content-type", "application/x-thrift")
        self.end_headers()

# INFO:(zuercher): Added to simplify usage
class THeaderTransportFactory:
    def getTransport(self, trans):
        return THeaderTransport(trans, client_type=CLIENT_TYPE.HEADER)

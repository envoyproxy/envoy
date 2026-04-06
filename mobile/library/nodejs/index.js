const path = require('path');

// Load the native addon
const addonPath = path.join(__dirname, 'envoy_mobile_node.node');
const addon = require(addonPath);

const LogLevel = {
  Trace: 0,
  Debug: 1,
  Info: 2,
  Warn: 3,
  Error: 4,
  Critical: 5,
  Off: 6,
};

class EnvoyResponse {
  constructor(status, headers, bodyBuffer) {
    this.status = status;
    this.headers = headers;
    this.bodyBuffer = bodyBuffer;
  }

  async text() {
    return this.bodyBuffer.toString('utf-8');
  }

  async json() {
    return JSON.parse(await this.text());
  }

  async arrayBuffer() {
    return this.bodyBuffer.buffer.slice(
      this.bodyBuffer.byteOffset,
      this.bodyBuffer.byteOffset + this.bodyBuffer.byteLength
    );
  }
}

class EnvoyClient {
  constructor(options = {}) {
    const builder = new addon.EnvoyEngineBuilder();
    if (options.logLevel !== undefined) {
      builder.setLogLevel(options.logLevel);
    } else {
      builder.setLogLevel(LogLevel.Info);
    }

    this.engine = builder.build();
    this.streamClient = this.engine.getStreamClient();
  }

  async fetch(url, options = {}) {
    const parsedUrl = new URL(url);
    const method = options.method || 'GET';
    const headers = {
      ':method': method,
      ':scheme': parsedUrl.protocol.replace(':', ''),
      ':authority': parsedUrl.host,
      ':path': parsedUrl.pathname + parsedUrl.search,
      ...options.headers,
    };

    return new Promise((resolve, reject) => {
      let responseStatus = 0;
      let responseHeaders = {};
      const bodyChunks = [];
      let engineStarted = false;

      const streamPrototype = this.streamClient.newStreamPrototype();
      const stream = streamPrototype.start({
        onHeaders: (headers, endStream) => {
          responseHeaders = headers;
          if (headers[':status']) {
            responseStatus = parseInt(headers[':status'], 10);
          }
          if (endStream) {
            resolve(new EnvoyResponse(responseStatus, responseHeaders, Buffer.concat(bodyChunks)));
          }
        },
        onData: (data, endStream) => {
          bodyChunks.push(data);
          if (endStream) {
            resolve(new EnvoyResponse(responseStatus, responseHeaders, Buffer.concat(bodyChunks)));
          }
        },
        onComplete: () => {
          resolve(new EnvoyResponse(responseStatus, responseHeaders, Buffer.concat(bodyChunks)));
        },
        onError: (error) => {
          reject(new Error(`Envoy stream error: ${error.message} (code: ${error.code})`));
        },
      });

      const hasBody = !!options.body;
      stream.sendHeaders(headers, !hasBody);

      if (options.body) {
        let bodyBuffer;
        if (typeof options.body === 'string') {
          bodyBuffer = Buffer.from(options.body);
        } else if (Buffer.isBuffer(options.body)) {
          bodyBuffer = options.body;
        } else {
          bodyBuffer = Buffer.from(options.body);
        }
        stream.close(bodyBuffer);
      }
    });
  }

  terminate() {
    this.engine.terminate();
  }
}

module.exports = {
  EnvoyClient,
  LogLevel,
  EnvoyResponse,
};

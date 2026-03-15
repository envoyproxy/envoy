import * as path from 'path';

// Load the native addon
const addonPath = path.join(__dirname, 'envoy_mobile_node.node');
const addon = require(addonPath);

export enum LogLevel {
  Trace = 0,
  Debug = 1,
  Info = 2,
  Warn = 3,
  Error = 4,
  Critical = 5,
  Off = 6,
}

export interface EnvoyClientOptions {
  logLevel?: LogLevel;
}

export interface FetchOptions {
  method?: string;
  headers?: Record<string, string>;
  body?: string | Buffer | Uint8Array;
}

export class EnvoyResponse {
  constructor(
    public readonly status: number,
    public readonly headers: Record<string, string>,
    private readonly bodyBuffer: Buffer
  ) {}

  async text(): Promise<string> {
    return this.bodyBuffer.toString('utf-8');
  }

  async json(): Promise<any> {
    return JSON.parse(await this.text());
  }

  async arrayBuffer(): Promise<ArrayBuffer> {
    return this.bodyBuffer.buffer.slice(
      this.bodyBuffer.byteOffset,
      this.bodyBuffer.byteOffset + this.bodyBuffer.byteLength
    );
  }
}

export class EnvoyClient {
  private engine: any;
  private streamClient: any;

  constructor(options: EnvoyClientOptions = {}) {
    const builder = new addon.EnvoyEngineBuilder();
    if (options.logLevel !== undefined) {
      builder.setLogLevel(options.logLevel);
    } else {
      builder.setLogLevel(LogLevel.Info);
    }

    // We use a sync build here for simplicity, 
    // though the engine runs its own threads.
    this.engine = builder.build();
    this.streamClient = this.engine.getStreamClient();
  }

  async fetch(url: string, options: FetchOptions = {}): Promise<EnvoyResponse> {
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
      let responseHeaders: Record<string, string> = {};
      const bodyChunks: Buffer[] = [];

      const streamPrototype = this.streamClient.newStreamPrototype();
      const stream = streamPrototype.start({
        onHeaders: (headers: any, endStream: boolean) => {
          responseHeaders = headers;
          if (headers[':status']) {
            responseStatus = parseInt(headers[':status'], 10);
          }
          if (endStream) {
            resolve(new EnvoyResponse(responseStatus, responseHeaders, Buffer.concat(bodyChunks)));
          }
        },
        onData: (data: Buffer, endStream: boolean) => {
          bodyChunks.push(data);
          if (endStream) {
            resolve(new EnvoyResponse(responseStatus, responseHeaders, Buffer.concat(bodyChunks)));
          }
        },
        onComplete: () => {
          resolve(new EnvoyResponse(responseStatus, responseHeaders, Buffer.concat(bodyChunks)));
        },
        onError: (error: any) => {
          reject(new Error(`Envoy stream error: ${error.message} (code: ${error.code})`));
        },
      });

      const hasBody = !!options.body;
      stream.sendHeaders(headers, !hasBody);

      if (options.body) {
        let bodyBuffer: Buffer;
        if (typeof options.body === 'string') {
          bodyBuffer = Buffer.from(options.body);
        } else if (Buffer.isBuffer(options.body)) {
          bodyBuffer = options.body;
        } else {
          bodyBuffer = Buffer.from(options.body as Uint8Array);
        }
        stream.close(bodyBuffer);
      }
    });
  }

  terminate() {
    this.engine.terminate();
  }
}

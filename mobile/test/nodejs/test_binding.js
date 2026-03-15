const addon = require('../../library/nodejs/envoy_mobile_node.node');
console.log('Addon loaded:', addon);

async function run() {
  try {
    const builder = new addon.EnvoyEngineBuilder();
    builder.setLogLevel(2); // info
    
    const engineRunningPromise = new Promise(resolve => {
      builder.setOnEngineRunning(() => {
        console.log('JS: Engine is now running!');
        resolve();
      });
    });
    
    console.log('Calling builder.build()...');
    const engine = builder.build();
    
    await engineRunningPromise;
    
    const streamClient = engine.getStreamClient();
    const streamPrototype = streamClient.newStreamPrototype();
    
    const streamCompletePromise = new Promise((resolve, reject) => {
      console.log('Starting stream to google.com...');
      const stream = streamPrototype.start({
        onHeaders: (headers, endStream) => {
          console.log('JS: Received headers:', headers);
          if (endStream) resolve();
        },
        onData: (data, endStream) => {
          console.log(`JS: Received data chunk (${data.length} bytes), endStream: ${endStream}`);
          if (endStream) resolve();
        },
        onComplete: () => {
          console.log('JS: Stream complete!');
          resolve();
        },
        onError: (error) => {
          console.error('JS: Stream error:', error);
          reject(error);
        },
        onCancel: () => {
          console.log('JS: Stream cancelled');
          resolve();
        }
      });
      
      stream.sendHeaders({
        ':method': 'GET',
        ':scheme': 'https',
        ':authority': 'www.google.com',
        ':path': '/',
        'user-agent': 'envoy-mobile-nodejs-test'
      }, true);
    });
    
    await streamCompletePromise;
    console.log('Request finished');
    
    console.log('Terminating engine...');
    engine.terminate();
    console.log('Engine terminated');
    
    // Give it a tiny bit of time for threads to fully clean up
    await new Promise(resolve => setTimeout(resolve, 500));
    console.log('Test successful');
    process.exit(0);

  } catch (e) {
    console.error('Test failed:', e);
    process.exit(1);
  }
}

run();

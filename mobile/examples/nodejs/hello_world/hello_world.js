/**
 * Basic example showing how to instantiate the Envoy engine and dump stats.
 */

const { EnvoyClient, LogLevel } = require('../../../library/nodejs/index.js');

async function main() {
  console.log('Instantiating Envoy Mobile...');
  
  // High-level client initializes the engine internally
  const client = new EnvoyClient({
    logLevel: LogLevel.Debug
  });

  // Give the engine a moment to start background threads
  await new Promise(resolve => setTimeout(resolve, 1000));

  console.log('\n--- Internal Engine Stats ---');
  // Access the low-level engine to dump stats (if needed for debugging)
  const stats = client.engine.dumpStats();
  console.log(stats);
  console.log('--- End Stats ---\n');

  console.log('Shutting down...');
  client.terminate();
  console.log('Done.');
}

main().catch(err => {
  console.error(err);
  process.exit(1);
});

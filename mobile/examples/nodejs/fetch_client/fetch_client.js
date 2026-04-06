/**
 * Node.js example using the Envoy Mobile high-level API.
 * 
 * Usage:
 *    node fetch_client.js <url>
 */

const { EnvoyClient, LogLevel } = require('../../../library/nodejs/index.js');

async function main() {
  const args = process.argv.slice(2);
  if (args.length === 0) {
    console.error('Usage: node fetch_client.js <url>');
    process.exit(1);
  }

  const url = args[0];

  // 1. Initialize the Envoy Client
  console.error(`Initializing Envoy Mobile Engine...`);
  const client = new EnvoyClient({
    logLevel: LogLevel.Info
  });

  try {
    // 2. Perform the fetch
    console.error(`Fetching ${url}...`);
    const response = await client.fetch(url, {
      method: 'GET',
      headers: {
        'user-agent': 'envoy-mobile-nodejs-example'
      }
    });

    // 3. Print response info
    console.error(`\n--- Response Received ---`);
    console.error(`Status: ${response.status}`);
    console.error(`Headers:`, response.headers);
    console.error(`-------------------------\n`);

    const text = await response.text();
    process.stdout.write(text);
    console.error(`\n\n[Finished: ${text.length} bytes received]`);

  } catch (error) {
    console.error('Fetch failed:', error.message);
    process.exit(1);
  } finally {
    // 4. Always terminate the client to shut down the proxy gracefully
    client.terminate();
  }
}

if (require.main === module) {
  main();
}

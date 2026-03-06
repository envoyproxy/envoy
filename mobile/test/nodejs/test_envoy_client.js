const { EnvoyClient, LogLevel } = require('../../library/nodejs/index.js');

async function main() {
  const client = new EnvoyClient({
    logLevel: LogLevel.Info
  });

  try {
    console.log('Fetching google.com...');
    // We give it some time to initialize internally since we don't have the onEngineRunning in the high-level constructor yet
    await new Promise(resolve => setTimeout(resolve, 2000));

    const response = await client.fetch('https://www.google.com/');
    
    console.log('Response Status:', response.status);
    console.log('Response Headers:', response.headers);
    
    const text = await response.text();
    console.log('Body length:', text.length);
    console.log('Body snippet:', text.substring(0, 100));

    if (response.status === 200 && text.length > 0) {
      console.log('SUCCESS: High-level EnvoyClient is working!');
    } else {
      console.error('FAILURE: Unexpected response');
      process.exit(1);
    }

    console.log('Test successful');
    process.exit(0);
  } catch (error) {
    console.error('Fetch failed:', error);
    process.exit(1);
  } finally {
    console.log('Terminating client...');
    client.terminate();
  }
}

main();

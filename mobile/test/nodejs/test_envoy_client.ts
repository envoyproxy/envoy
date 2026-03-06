import { EnvoyClient, LogLevel } from '../../library/nodejs';

async function main() {
  const client = new EnvoyClient({
    logLevel: LogLevel.Info
  });

  try {
    console.log('Fetching google.com...');
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

  } catch (error) {
    console.error('Fetch failed:', error);
    process.exit(1);
  } finally {
    console.log('Terminating client...');
    client.terminate();
  }
}

main();

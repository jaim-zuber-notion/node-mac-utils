// Import the package
const utils = require('./index.js');

// Test function to run all checks
async function runTests() {
    console.log('Running on platform:', process.platform);
    
    try {
        // Test getRunningInputAudioProcesses
        console.log('\nTesting getRunningInputAudioProcesses:');
        const result = utils.getRunningInputAudioProcesses();

        console.log('result', result);
        if (result.success) {
            console.log('Audio processes:', result.processes);
        } else {
            console.error('Error getting audio processes:', result.error);
            console.error('Error code:', result.code);
            console.error('Error domain:', result.domain);
        }

        // Test platform-specific functions
        if (process.platform === 'darwin') {
            console.log('\nTesting Mac-specific functions:');
            console.log('makeKeyAndOrderFront available:', !!utils.makeKeyAndOrderFront);
            console.log('startMonitoringMic available:', !!utils.startMonitoringMic);
            console.log('stopMonitoringMic available:', !!utils.stopMonitoringMic);
        } else if (process.platform === 'win32') {
            console.log('\nTesting Windows-specific functions:');
            console.log('getRunningInputAudioProcesses available:', !!utils.getRunningInputAudioProcesses);
        } else {
            console.log('node-mac-utils Unsupported platform:', process.platform);
        }

        // Log all available exports
        console.log('\nAll available exports:');
        console.log(Object.keys(utils));

    } catch (error) {
        console.error('Test failed:', error.message);
        if (error.code) {
            console.error('Error code:', error.code);
        }
        if (error.domain) {
            console.error('Error domain:', error.domain);
        }
        process.exit(1);
    }
}

// Run the tests
runTests(); 
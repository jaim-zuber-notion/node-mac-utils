// Import the package
const utils = require('./index.js');

// Test function to run all checks
async function runTests() {
    console.log('Running on platform:', process.platform);
    try {
        // Test original getRunningInputAudioProcesses (returns array)
        console.log('\nTesting getRunningInputAudioProcesses (original):');
        const processes = utils.getRunningInputAudioProcesses();
        console.log('Type:', typeof processes);
        console.log('Is Array:', Array.isArray(processes));
        console.log('Audio processes:', processes);

        // Test new getProcessesAccessingMicrophoneWithResult (returns structured result)
        console.log('\nTesting getProcessesAccessingMicrophoneWithResult (new):');
        const result = utils.getProcessesAccessingMicrophoneWithResult();
        console.log('Type:', typeof result);
        console.log('Result:', result);

        if (result.success) {
            console.log('✓ Success - Audio processes:', result.processes);
            console.log('  Process count:', result.processes.length);
        } else {
            console.error('✗ Error getting audio processes:', result.error);
            console.error('  Error code:', result.code);
            console.error('  Error domain:', result.domain);
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
            console.log('getProcessesAccessingMicrophoneWithResult available:', !!utils.getProcessesAccessingMicrophoneWithResult);
        } else {
            console.log('node-mac-utils Unsupported platform:', process.platform);
        }

        // Compare both methods
        console.log('\nComparing both methods:');
        console.log('Original method returns:', Array.isArray(processes) ? 'Array' : typeof processes);
        console.log('New method returns:', typeof result === 'object' && result.hasOwnProperty('success') ? 'Structured Object' : typeof result);

        if (result.success && Array.isArray(processes)) {
            console.log('Process count - Original:', processes.length, 'New:', result.processes.length);
            console.log('Data matches:', JSON.stringify(processes) === JSON.stringify(result.processes));
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
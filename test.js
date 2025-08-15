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
            console.log('✓ Success - Microphone processes:', result.processes);
            console.log('  Process count:', result.processes.length);
        } else {
            console.error('✗ Error getting microphone processes:', result.error);
            console.error('  Error code:', result.code);
            console.error('  Error domain:', result.domain);
        }

        // Test new speaker detection functionality
        console.log('\nTesting getProcessesAccessingSpeakerWithResult (new):');
        const speakerResult = utils.getProcessesAccessingSpeakerWithResult();
        console.log('Type:', typeof speakerResult);
        console.log('Result:', speakerResult);

        if (speakerResult.success) {
            console.log('✓ Success - Speaker processes:', speakerResult.processes);
            console.log('  Speaker process count:', speakerResult.processes.length);
            if (speakerResult.processes.length > 0) {
                console.log('  Active speaker processes:');
                speakerResult.processes.forEach((proc, index) => {
                    console.log(`    ${index + 1}. ${proc}`);
                });
            }
        } else {
            console.log('✗ Speaker detection:', speakerResult.error);
            if (speakerResult.code) console.log('  Error code:', speakerResult.code);
            if (speakerResult.domain) console.log('  Error domain:', speakerResult.domain);
        }

        // Test Bluetooth device detection
        console.log('\nTesting isBluetoothDevice:');
        const testDeviceIds = ['fake-device-id', 'test-bluetooth-device'];
        testDeviceIds.forEach(deviceId => {
            try {
                const isBluetooth = utils.isBluetoothDevice(deviceId);
                console.log(`  ${deviceId}: ${isBluetooth ? 'Bluetooth' : 'Not Bluetooth'}`);
            } catch (error) {
                console.log(`  ${deviceId}: Error - ${error.message}`);
            }
        });

        // Test platform-specific functions and cross-platform capabilities
        if (process.platform === 'darwin') {
            console.log('\nTesting macOS functions:');
            console.log('  makeKeyAndOrderFront available:', !!utils.makeKeyAndOrderFront);
            console.log('  startMonitoringMic available:', !!utils.startMonitoringMic);
            console.log('  stopMonitoringMic available:', !!utils.stopMonitoringMic);
            console.log('  getProcessesAccessingSpeakerWithResult:', speakerResult.error || 'Available but returns error (expected on macOS)');
            console.log('  isBluetoothDevice: Returns false (no-op on macOS)');
        } else if (process.platform === 'win32') {
            console.log('\nTesting Windows functions:');
            console.log('  getRunningInputAudioProcesses available:', !!utils.getRunningInputAudioProcesses);
            console.log('  getProcessesAccessingMicrophoneWithResult available:', !!utils.getProcessesAccessingMicrophoneWithResult);
            console.log('  getProcessesAccessingSpeakerWithResult available:', !!utils.getProcessesAccessingSpeakerWithResult);
            console.log('  isBluetoothDevice available:', !!utils.isBluetoothDevice);
            console.log('  startMonitoringMic available (NEW for Windows):', !!utils.startMonitoringMic);
            console.log('  stopMonitoringMic available (NEW for Windows):', !!utils.stopMonitoringMic);
        } else {
            console.log('Unsupported platform:', process.platform);
            console.log('All functions should return no-op values');
        }

        // Audio Summary Section
        console.log('\n' + '='.repeat(60));
        console.log('🎵 AUDIO PROCESS SUMMARY');
        console.log('='.repeat(60));
        
        console.log('\n🎤 MICROPHONE PROCESSES:');
        if (result.success && result.processes.length > 0) {
            result.processes.forEach((proc, index) => {
                console.log(`  ${index + 1}. ${proc}`);
            });
        } else {
            console.log('  No active microphone processes detected');
        }
        
        console.log('\n🔊 SPEAKER PROCESSES:');
        if (speakerResult.success && speakerResult.processes.length > 0) {
            speakerResult.processes.forEach((proc, index) => {
                console.log(`  ${index + 1}. ${proc}`);
            });
        } else if (speakerResult.error) {
            console.log(`  ${speakerResult.error}`);
        } else {
            console.log('  No active speaker processes detected');
        }

        // Compare both methods
        console.log('\n📊 METHOD COMPARISON:');
        console.log('Original method returns:', Array.isArray(processes) ? 'Array' : typeof processes);
        console.log('New method returns:', typeof result === 'object' && result.hasOwnProperty('success') ? 'Structured Object' : typeof result);

        if (result.success && Array.isArray(processes)) {
            console.log('Process count - Original:', processes.length, 'New:', result.processes.length);
            console.log('Data matches:', JSON.stringify(processes) === JSON.stringify(result.processes));
        }

        // Log all available exports
        console.log('\n📋 ALL AVAILABLE EXPORTS:');
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
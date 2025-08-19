/**
 * Test file for microphone monitoring functionality
 *
 * This file demonstrates how to:
 * 1. Request microphone permission with callback
 * 2. Start monitoring microphone usage
 * 3. Get a list of processes using the microphone
 */

const { 
  getRunningInputAudioProcesses, 
  getProcessesAccessingMicrophoneWithResult,
  startMonitoringMic,
  stopMonitoringMic,
  INFO_ERROR_CODE, 
  ERROR_DOMAIN 
} = require('./index');
const EventEmitter = require('events');

let isMonitoring = false;
let lastMicState = null;

// Function to display current microphone processes
async function displayCurrentMicProcesses() {
  try {
    console.log('\n--- Current Microphone Processes ---');
    const result = getProcessesAccessingMicrophoneWithResult();

    if (result.success) {
      if (result.processes.length === 0) {
        console.log('No processes are currently using the microphone');
      } else {
        console.log('Processes using the microphone:');
        result.processes.forEach((process, index) => {
          console.log(`${index + 1}. ${process}`);
        });
      }
    } else {
      console.error('Error getting microphone processes:', result.error);
      if (result.code) console.error('Error code:', result.code);
      if (result.domain) console.error('Error domain:', result.domain);
    }
    console.log('----------------------------------------\n');
  } catch (error) {
    console.error('Error getting microphone processes:', error.message);
  }
}

// Enhanced monitoring with process details
function startEnhancedMonitoring() {
  if (isMonitoring) {
    console.log('Monitoring is already active');
    return;
  }

  console.log(`🎤 Starting microphone monitoring on ${process.platform}...`);
  console.log('Listening for microphone session changes...\n');

  try {
    startMonitoringMic((microphoneActive, error) => {
      const timestamp = new Date().toISOString();

      if (error) {
        console.error(`❌ [${timestamp}] Microphone monitoring error:`, error);
        if (typeof error === 'object') {
          if (error.code) console.error('   Error code:', error.code);
          if (error.domain) console.error('   Error domain:', error.domain);
        }
        return;
      }

      // Only report state changes or initial state
      if (lastMicState !== microphoneActive) {
        lastMicState = microphoneActive;
        
        const status = microphoneActive ? '🟢 ACTIVE' : '🔴 INACTIVE';
        const action = microphoneActive ? 'connected' : 'disconnected';
        
        console.log(`🎤 [${timestamp}] Microphone ${status} - Session ${action}`);
        
        // Show current processes when microphone becomes active
        if (microphoneActive) {
          setTimeout(displayCurrentMicProcesses, 100); // Small delay to catch the new process
        }
      }
    });

    isMonitoring = true;
    console.log('✅ Microphone monitoring started successfully!');
    console.log('💡 Try joining a video call, using voice recording, or starting an audio app...');
    
  } catch (error) {
    console.error('❌ Failed to start microphone monitoring:', error.message);
    if (error.code) console.error('   Error code:', error.code);
    if (error.domain) console.error('   Error domain:', error.domain);
  }
}

// Graceful shutdown
function gracefulShutdown() {
  if (isMonitoring) {
    console.log('\n🛑 Stopping microphone monitoring...');
    stopMonitoringMic();
    isMonitoring = false;
    console.log('✅ Monitoring stopped');
  }
  process.exit(0);
}

// Handle process termination signals
process.on('SIGINT', gracefulShutdown);
process.on('SIGTERM', gracefulShutdown);

// Keep the process alive
process.stdin.resume();
process.stdin.setEncoding('utf8');

// Optional: Allow manual commands
process.stdin.on('data', (data) => {
  const command = data.toString().trim().toLowerCase();
  
  if (command === 'status' || command === 's') {
    displayCurrentMicProcesses();
  } else if (command === 'quit' || command === 'q' || command === 'exit') {
    gracefulShutdown();
  } else if (command === 'help' || command === 'h') {
    console.log('\nAvailable commands:');
    console.log('  status (s) - Show current microphone processes');
    console.log('  help   (h) - Show this help message');
    console.log('  quit   (q) - Exit the monitor');
    console.log('  Ctrl+C     - Exit the monitor\n');
  }
});

// Start the monitoring
console.log('🎵 Microphone Session Monitor');
console.log('============================');
console.log('Platform:', process.platform);
console.log('Press Ctrl+C to stop, or type "help" for commands\n');

// Show initial state
displayCurrentMicProcesses();

// Start monitoring
startEnhancedMonitoring();



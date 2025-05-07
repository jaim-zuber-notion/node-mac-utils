/**
 * Test file for microphone monitoring functionality
 *
 * This file demonstrates how to:
 * 1. Request microphone permission with callback
 * 2. Start monitoring microphone usage
 * 3. Get a list of processes using the microphone
 */

const { startMonitoringMic, stopMonitoringMic, getRunningInputAudioProcesses } = require('./index');
const EventEmitter = require('events');

class MicrophoneStatusEmitter extends EventEmitter {
  start() {
    startMonitoringMic((microphoneActive, error) => {
      if (error) {
        this.emit('info', error.message);
      } else {
        this.emit('status', microphoneActive);
      }
    });
  }

  stop() {
    stopMonitoringMic();
  }
}

// Function to display microphone processes
async function displayMicProcesses() {
  try {
    console.log('\n--- Current Microphone Processes ---');
    const processes = await getRunningInputAudioProcesses();

    if (processes.length === 0) {
      console.log('No processes are currently using the microphone');
    } else {
      console.log('Processes using the microphone:');
      processes.forEach((process, index) => {
        console.log(`${index + 1}. ${process}`);
      });
    }
  } catch (error) {
    console.error('Error getting microphone processes:', error.message);
  }
}

function startMicrophoneStatusEmitter() {
  const emitter = new MicrophoneStatusEmitter();

  emitter.on('status', (isActive) => {
    console.log('🎤 Microphone Status: ' + isActive);
  });

  emitter.on('info', (info) => {
    console.log('⚠️ Microphone Info:', info);
  });

  emitter.start();
}

function startMicMonitor() {
  try {
    startMonitoringMic((microphoneActive, error) => {
      const timestamp = new Date().toISOString();

      if (error) {
        console.error('Node - error');
        console.error('Error starting microphone monitor:', error.message);
      } else {
        console.log(`Node: [${timestamp}] Microphone active:`, microphoneActive);
      }
    });
  } catch (error) {
    console.error('Node - error');
    console.error('Error starting microphone monitor:', error.message);
  }
}

// Handle process termination
process.on('SIGINT', () => {
  console.log('\nStopping microphone monitoring...');
  stopMonitoringMic();
  process.exit(0);
});

console.log('Starting microphone monitoring...');
console.log('Press Ctrl+C to stop.\n');

displayMicProcesses();

startMicrophoneStatusEmitter();

// Keep the process running
process.stdin.resume();

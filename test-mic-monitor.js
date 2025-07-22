/**
 * Test file for microphone monitoring functionality
 *
 * This file demonstrates how to:
 * 1. Request microphone permission with callback
 * 2. Start monitoring microphone usage
 * 3. Get a list of processes using the microphone
 */

const { getRunningInputAudioProcesses, INFO_ERROR_CODE, ERROR_DOMAIN } = require('./index');
const EventEmitter = require('events');

// Only import mic monitoring functions on Darwin (macOS) systems
const { startMonitoringMic, stopMonitoringMic } = process.platform === 'darwin'
  ? require('./index')
  : {
      startMonitoringMic: () => {
        throw new Error('Microphone monitoring is only supported on macOS');
      },
      stopMonitoringMic: () => {
        // No-op for non-Darwin systems
      }
    };

class MicrophoneStatusEmitter extends EventEmitter {
  start() {
    startMonitoringMic((microphoneActive, error) => {
      if (error) {
        if (error.code === INFO_ERROR_CODE && error.domain === ERROR_DOMAIN) {
          this.emit('info', error.message);
        } else {
          this.emit('error', error.message);
        }
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
    const processes = getRunningInputAudioProcesses(); // Returns simple array

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
    if (error.code) {
      console.error('Error code:', error.code);
    }
    if (error.domain) {
      console.error('Error domain:', error.domain);
    }
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

  emitter.on('error', (error) => {
    console.error('❌ Microphone Error:', error);
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
        if (error.code) {
          console.error('Error code:', error.code);
        }
        if (error.domain) {
          console.error('Error domain:', error.domain);
        }
      } else {
        console.log(`Node: [${timestamp}] Microphone active:`, microphoneActive);
      }
    });
  } catch (error) {
    console.error('Node - error');
    console.error('Error starting microphone monitor:', error.message);
    if (error.code) {
      console.error('Error code:', error.code);
    }
    if (error.domain) {
      console.error('Error domain:', error.domain);
    }
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

if (process.platform === 'darwin') {
  startMicrophoneStatusEmitter();
  // Keep the process running
  process.stdin.resume();
}



let platform_utils;

const noopPlatformUtils = {
  getRunningInputAudioProcesses: () => {
    return ['', ''];
  },
  getProcessesAccessingMicrophoneWithResult: () => {
    return {
      success: true,
      error: null,
      processes: ['', '']
    };
  }
};

if (process.platform === 'darwin') {
  platform_utils = require("bindings")("mac_utils.node");
} else if (process.platform === 'win32') {
  platform_utils = require("bindings")("win_utils.node");
} else {
  console.log('node-mac-utils Unsupported platform:', process.platform);
  platform_utils = noopPlatformUtils;
}

module.exports = {
  // Common exports that work on all platforms
  getRunningInputAudioProcesses: platform_utils.getRunningInputAudioProcesses,
  getProcessesAccessingMicrophoneWithResult: platform_utils.getProcessesAccessingMicrophoneWithResult,
  INFO_ERROR_CODE: 1,
  ERROR_DOMAIN: "com.MicrophoneUsageMonitor",
  
  // Mac-specific exports
  ...(process.platform === 'darwin' ? {
    makeKeyAndOrderFront: platform_utils.makeKeyAndOrderFront,
    startMonitoringMic: platform_utils.startMonitoringMic,
    stopMonitoringMic: platform_utils.stopMonitoringMic,
  } : {}),
  
  // Windows-specific event monitoring exports
  ...(process.platform === 'win32' ? {
    createAudioSessionMonitor: platform_utils.createAudioSessionMonitor,
    createEnhancedAudioSessionMonitor: platform_utils.createEnhancedAudioSessionMonitor,
    destroyAudioSessionMonitor: platform_utils.destroyAudioSessionMonitor,
    startMonitoringMic: platform_utils.startMonitoringMic,
    stopMonitoringMic: platform_utils.stopMonitoringMic,
  } : {})
};

const EventEmitter = require('events');

class MicrophoneStatusEmitter extends EventEmitter {
  constructor(platform_utils) {
    super();
    this.platform_utils = platform_utils;
  }

  start() {
    this.platform_utils.startMonitoringMic((microphoneActive, error) => {
      if (error) {
        this.emit('info', error.message);
      } else {
        this.emit('status', microphoneActive);
      }
    });
  }

  stop() {
    this.platform_utils.stopMonitoringMic();
  }
}

module.exports = MicrophoneStatusEmitter;
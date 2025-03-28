const mac_utils = require("bindings")("mac_utils.node");

module.exports = {
  makeKeyAndOrderFront: mac_utils.makeKeyAndOrderFront,
  getRunningInputAudioProcesses: mac_utils.getRunningInputAudioProcesses
};

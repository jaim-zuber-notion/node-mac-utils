{
  "targets": [{
    "target_name": "mac_utils",
    "sources": [ ],
    "conditions": [
      ['OS=="mac"', {
        "sources": [
          "macOS/mac_utils.mm",
          "macOS/AudioProcessMonitor.m",
          "macOS/MicrophoneUsageMonitor.m",
        ],
        "xcode_settings": {
          "OTHER_CFLAGS": ["-fobjc-arc"]
        }
      }]
    ],
    'include_dirs': [
      "<!@(node -p \"require('node-addon-api').include\")"
    ],
    'libraries': [],
    'dependencies': [
      "<!(node -p \"require('node-addon-api').gyp\")"
    ],
    'defines': [ 'NAPI_DISABLE_CPP_EXCEPTIONS' ],
    "xcode_settings": {
      "MACOSX_DEPLOYMENT_TARGET": "10.13",
      "SYSTEM_VERSION_COMPAT": 1,
      "OTHER_CPLUSPLUSFLAGS": ["-std=c++14", "-stdlib=libc++"],
      "OTHER_LDFLAGS": [
        "-framework CoreFoundation",
        "-framework AppKit",
        "-framework AudioToolbox",
        "-framework AVFoundation"
      ]
    }
  }, {
    "target_name": "win_utils",
    "sources": [ ],
    "conditions": [
      ['OS=="win"', {
        "sources": [
          "windows/win_utils.cpp",
          "windows/AudioProcessMonitor.cpp"
        ]
      }]
    ],
    'include_dirs': [
      "<!@(node -p \"require('node-addon-api').include\")"
    ],
    'libraries': [],
    'dependencies': [
      "<!(node -p \"require('node-addon-api').gyp\")"
    ],
    'defines': [ 'NAPI_DISABLE_CPP_EXCEPTIONS' ]
  }]
}
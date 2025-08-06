# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

node-mac-utils is a native Node.js addon that provides cross-platform utilities for monitoring audio input processes on macOS and Windows. The module detects which processes are currently accessing the microphone, with additional macOS-specific functionality for microphone usage monitoring and window management.

## Development Commands

```bash
# Build the native addon
npm run build

# Clean build artifacts
npm run clean

# Lint and format code
npm run lint      # Check C++ and JavaScript formatting
npm run format    # Auto-format C++ and JavaScript code

# Run tests
npm test          # Runs test-mic-monitor.js
node test.js      # Comprehensive testing of all functions
```

## Architecture

### Platform Detection and Loading
The main entry point (`index.js`) uses platform detection to load the appropriate native binary:
- **macOS**: Loads `mac_utils.node` built from `macOS/mac_utils.mm`
- **Windows**: Loads `win_utils.node` built from `windows/win_utils.cpp`
- **Other platforms**: Uses no-op fallback implementation

### Native Module Structure
The project builds two separate native addons using node-gyp:

1. **mac_utils.node** (macOS only)
   - Built from `macOS/mac_utils.mm`, `AudioProcessMonitor.m`, `MicrophoneUsageMonitor.m`
   - Links against CoreFoundation, AppKit, AudioToolbox, AVFoundation frameworks
   - Uses Objective-C++ with ARC enabled

2. **win_utils.node** (Windows only)
   - Built from `windows/win_utils.cpp`, `AudioProcessMonitor.cpp`
   - Uses Windows Audio APIs

### API Design
The module provides two interfaces for getting microphone processes:

1. **Legacy interface**: `getRunningInputAudioProcesses()` returns a simple array of process names
2. **New interface**: `getProcessesAccessingMicrophoneWithResult()` returns structured result with success/error handling:
   ```javascript
   {
     success: boolean,
     error: string | null,
     processes: string[],
     code?: number,
     domain?: string
   }
   ```

### macOS-Specific Features
- `makeKeyAndOrderFront()`: Window focus management for Electron apps
- `startMonitoringMic()`: Real-time microphone usage monitoring with callbacks
- `stopMonitoringMic()`: Stop microphone monitoring

## Native Code Organization

### macOS Implementation
- **AudioProcessMonitor**: Handles process detection using macOS audio APIs
- **MicrophoneUsageMonitor**: Real-time monitoring with NSTimer and callback system
- Both use proper macOS error handling with NSError objects

### Windows Implementation  
- **AudioProcessMonitor**: Uses Windows audio session APIs to detect microphone usage
- Structured error handling with custom AudioProcessResult struct

## Testing
- `test.js`: Comprehensive test suite covering all platforms and functions
- `test-mic-monitor.js`: Focused test for microphone monitoring functionality with EventEmitter pattern
- Tests include platform detection, function availability checks, and result comparison

## Build Configuration
The `binding.gyp` uses conditional compilation:
- macOS builds require deployment target 10.13+ and specific framework linking
- Windows builds use standard Windows APIs
- Both use node-addon-api with C++ exceptions disabled

## Bluetooth Headset Reliability (Windows)
The current branch includes significant improvements for Bluetooth headset detection reliability:

### Key Improvements
1. **Multi-Device Enumeration**: Changed from checking only the default capture device to enumerating ALL active capture devices
2. **Enhanced Activity Detection**: Implements a 4-tier detection system:
   - **Method 1**: Peak value monitoring (`IAudioMeterInformation`)
   - **Method 2**: Buffer activity checking (`IAudioClient::GetCurrentPadding`)  
   - **Method 3**: Active session validation with volume/mute checks
   - **Method 4**: Bluetooth-specific permissive detection

### Bluetooth-Specific Logic
- `IsBluetoothDevice()`: Detects Bluetooth devices by checking device properties for "Bluetooth" or "Wireless" keywords
- **Permissive Mode**: For Bluetooth devices, considers any sessions as potentially active (not just those with detectable audio activity)
- **Rationale**: Bluetooth audio devices often report unreliable activity metrics due to driver/protocol limitations

### Implementation Details
- **windows/AudioProcessMonitor.cpp:183-186**: Bluetooth devices use `HasAnySessions()` instead of strict activity detection
- **windows/AudioProcessMonitor.cpp:216-217**: `EnumAudioEndpoints()` replaces `GetDefaultAudioEndpoint()` for comprehensive device coverage
- **Enhanced Error Handling**: New `AudioProcessResult` struct provides structured error reporting with HRESULT codes

This hardening addresses the common issue where Bluetooth headsets would intermittently fail to report active audio sessions due to inconsistent driver behavior and wireless protocol timing.

## Code Quality Guidelines

### Whitespace and Formatting
- **No Trailing Whitespace**: Empty lines must contain no spaces or tabs
- **Consistent Indentation**: Use spaces consistently (follow existing file patterns)
- **Clean Line Endings**: Ensure proper line termination without trailing characters
- **Run Linting**: Always run `npm run lint` and `npm run format` before committing changes

### C++ Specific Guidelines
- Use consistent brace placement and indentation
- Avoid whitespace on empty lines between code blocks
- Maintain clean separation between logical code sections

## Known Limitations & Improvement Opportunities

### Current Bluetooth Detection Issues - ✅ RESOLVED
~~1. **Weak Heuristic**: Current `IsBluetoothDevice()` only checks for "Bluetooth" or "Wireless" keywords in device names~~
   - ✅ **Fixed**: Implemented robust 7-tier detection system using Windows property keys:
     - `PKEY_Device_InstanceId` - Hardware-level Bluetooth enumeration patterns
     - `PKEY_Device_HardwareIds` - Vendor-specific Bluetooth identifiers  
     - `PKEY_Device_ClassGuid` - Bluetooth device class validation
     - `PKEY_Device_BusTypeGuid` - Bus type verification
     - `PKEY_DeviceInterface_FriendlyName` - Enhanced name pattern matching
     - Hardware detection takes precedence over name-based fallbacks

2. **Overly Permissive Logic**: Bluetooth devices use `HasAnySessions()` fallback which can cause false positives
   - Currently: "If Bluetooth && has any sessions → consider active"
   - **Suggested Fix**: Apply same volume/mute filters as other devices but with relaxed thresholds
   - **Alternative**: Add debouncing/state caching to handle session flapping common with Bluetooth

3. **Missing State Management**: No debouncing for Bluetooth session state changes
   - Bluetooth sessions frequently flap between idle/active due to wireless protocol behavior
   - **Suggested Fix**: Implement time-based state caching or require sustained activity periods

### Recommended Improvements
- ✅ **Enhanced Bluetooth Detection**: Replaced keyword-based detection with property key validation
- **Graduated Permissiveness**: Apply standard volume/mute checks to Bluetooth devices with lower thresholds
- **State Debouncing**: Add temporal filtering for rapid state changes specific to wireless devices
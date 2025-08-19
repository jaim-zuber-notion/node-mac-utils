const utils = require('./index.js');

console.log('🔍 DEBUG: Microphone Detection Analysis');
console.log('=====================================');
console.log('Make sure you have an active Chrome video call/meeting...\n');

// Test new structured method
console.log('1. Testing getProcessesAccessingMicrophoneWithResult():');
const result = utils.getProcessesAccessingMicrophoneWithResult();
console.log('   Result:', JSON.stringify(result, null, 2));

// Test original method  
console.log('\n2. Testing getRunningInputAudioProcesses():');
const processes = utils.getRunningInputAudioProcesses();
console.log('   Result:', processes);

// Test speaker detection for comparison
console.log('\n3. Testing getProcessesAccessingSpeakerWithResult() (for comparison):');
const speakerResult = utils.getProcessesAccessingSpeakerWithResult();
console.log('   Result:', JSON.stringify(speakerResult, null, 2));

console.log('\n🤔 Analysis:');
if (result.success && result.processes.length === 0) {
    console.log('❌ No microphone processes detected even with Chrome video call active');
    console.log('   Possible causes:');
    console.log('   - Detection logic is too strict (using enhanced 4-tier detection)');
    console.log('   - Chrome audio sessions not being reported as "active"');
    console.log('   - Audio session enumeration missing Chrome');
    console.log('   - Chrome uses exclusive mode or low-level APIs');
    console.log('\n💡 Solutions to try:');
    console.log('   - Reduce detection thresholds in HasActiveAudio()');
    console.log('   - Fall back to session enumeration without activity checks');
    console.log('   - Debug the 4-tier detection system');
} else if (result.success && result.processes.length > 0) {
    console.log('✅ Microphone processes detected successfully');
    result.processes.forEach(proc => {
        console.log(`   Found: ${proc}`);
        if (proc.toLowerCase().includes('chrome')) {
            console.log('   ✅ Chrome detected - monitoring should work');
        }
    });
} else {
    console.log('❌ Error in detection:', result.error);
}

if (speakerResult.success && speakerResult.processes.length > 0) {
    console.log('\n📊 Speaker processes detected:');
    speakerResult.processes.forEach(proc => {
        console.log(`   Speaker: ${proc}`);
        if (proc.toLowerCase().includes('chrome')) {
            console.log('   ✅ Chrome detected for speakers - audio is active');
        }
    });
}
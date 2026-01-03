import re

path = r'c:\data\BISSO_E350_Controller\data\pages\dashboard\dashboard.js'

with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

# Define the messy block pattern
# Starts with "if (state.vfd) {" ... and ends before "// Network status"
# The mess contains "Re-reading HTML view 9425" or similar comments.

clean_block = """        // VFD status (Spindle)
        if (state.vfd) {
            const vfdStatusEl = document.getElementById('vfd-status');
            const vfdRpmEl = document.getElementById('spindle-rpm');
            const vfdSpeedEl = document.getElementById('spindle-speed');
            const vfdCurrentEl = document.getElementById('spindle-current');

            if (state.vfd.connected) {
                const motorStatus = (state.vfd.rpm > 0) ? 'RUNNING' : 'IDLE';
                if (vfdStatusEl) vfdStatusEl.textContent = motorStatus;

                if (vfdRpmEl) vfdRpmEl.textContent = (state.vfd.rpm || 0).toFixed(0);
                if (vfdSpeedEl) vfdSpeedEl.textContent = (state.vfd.speed_m_s || 0).toFixed(1) + ' m/s';
                if (vfdCurrentEl) vfdCurrentEl.textContent = (state.vfd.current_amps || 0).toFixed(2) + ' A';

                const bar = document.getElementById('spindle-bar');
                if (bar) {
                    const pct = Math.min(100, ((state.vfd.current_amps || 0) / 30.0) * 100);
                    bar.style.width = pct + '%';
                }
            } else {
                if (vfdStatusEl) vfdStatusEl.textContent = 'DISCONNECTED';
                if (vfdRpmEl) vfdRpmEl.textContent = 'N/A';
                if (vfdSpeedEl) vfdSpeedEl.textContent = 'N/A m/s';
                if (vfdCurrentEl) vfdCurrentEl.textContent = 'N/A A';

                const bar = document.getElementById('spindle-bar');
                if (bar) bar.style.width = '0%';
            }
        }
"""

# Regex to find the borked vfd block
# There are two "// VFD status" lines in the broken file
# "// VFD status\n        // VFD status (Spindle)\n        if (state.vfd) {"
# And it includes comments like "// This might be"

pattern = r'\s+// VFD status\s+// VFD status \(Spindle\)\s+if \(state\.vfd\) \{.*?// Network status'

# Attempt to match
match = re.search(pattern, content, re.DOTALL)
if match:
    print("Found broken block. Replacing...")
    # Replace (keep // Network status)
    new_content = re.sub(pattern, "\n" + clean_block + "\n        // Network status", content, flags=re.DOTALL)
    
    with open(path, 'w', encoding='utf-8') as f:
        f.write(new_content)
    print("Fixed.")
else:
    print("Pattern not found. Dumping snippet for debug:")
    start = content.find("if (state.vfd)")
    print(content[start:start+500])

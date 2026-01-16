import serial
import time
import sys
import re

# PosiPro Hardware Verification Script
# Verifies successful application of audits optimizations:
# 1. 400kHz I2C Bus
# 2. task CPU balancing
# 3. RS485 bus state

SERIAL_PORT = "COM5" # Default, modify as needed
BAUD_RATE = 115200

def send_command(ser, cmd, timeout=2.0):
    ser.write((cmd + "\n").encode())
    time.sleep(0.1)
    
    start = time.time()
    response = ""
    while time.time() - start < timeout:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore')
            response += line
            if "OK" in line or "ERROR" in line:
                break
    return response

def verify_i2c(ser):
    print("\n[TEST] Verifying I2C Bus Optimization...")
    res = send_command(ser, "i2c status")
    print(res)
    
    # Check for success rate
    match = re.search(r"OK: (\d+) \((\d+\.\d+)%\)", res)
    if match:
        ok_count = int(match.group(1))
        percent = float(match.group(2))
        if percent > 95.0:
            print("[PASS] I2C Success Rate > 95%")
        else:
            print(f"[WARN] I2C Success Rate Low: {percent}%")
    else:
        print("[FAIL] Could not parse I2C status")
        
def verify_cores(ser):
    print("\n[TEST] Verifying Task Core Affinity (Balancing)...")
    res = send_command(ser, "task list")
    print(res)
    
    # Parse table: Task Name | Priority | Stack | Core
    # We want to see:
    # Motion -> Core 1
    # LCD -> Core 0
    # I2C_Manager -> Core 0
    
    core_map = {}
    lines = res.splitlines()
    for line in lines:
        parts = line.split()
        if len(parts) >= 4 and parts[-1].isdigit():
            name = parts[0]
            core = int(parts[-1])
            core_map[name] = core
            
    expected = {
        "Motion": 1,
        "Safety": 1,
        "Encoder": 1,
        "I2C_Manager": 0,
        "LCD": 0,
        "Monitor": 0,
        "CLI": 0
    }
    
    all_pass = True
    for task, exp_core in expected.items():
        if task in core_map:
            actual = core_map[task]
            if actual == exp_core:
                print(f"[PASS] {task} on Core {actual}")
            else:
                print(f"[FAIL] {task} on Core {actual} (Expected {exp_core})")
                all_pass = False
        else:
            print(f"[WARN] {task} not found in task list")
            
    if all_pass:
        print("[SUCCESS] Core Balancing Verified")

def verify_rs485(ser):
    print("\n[TEST] Verifying RS485 Registry...")
    res = send_command(ser, "rs485 status")
    print(res)
    if "Device Registry" in res:
        print("[PASS] RS485 Registry Active")
    else:
        print("[FAIL] RS485 Registry not responding")

def main():
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
        print(f"Connected to {SERIAL_PORT}")
        
        # Flush
        ser.read_all()
        
        verify_cores(ser)
        verify_i2c(ser)
        verify_rs485(ser)
        
        ser.close()
    except Exception as e:
        print(f"[ERROR] Connection failed: {e}")
        print("Usage: python hardware_verification.py [COM_PORT]")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        SERIAL_PORT = sys.argv[1]
    main()

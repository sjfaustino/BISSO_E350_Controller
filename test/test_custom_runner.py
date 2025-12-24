#!/usr/bin/env python3
"""
Custom Test Runner for BISSO E350 Unit Tests
This script is used by PlatformIO's custom test framework to execute Unity tests.
"""

import subprocess
import sys
import os

def main():
    """Run the test executable and return exit code."""
    # Find the test executable
    test_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(test_dir)
    
    # PlatformIO builds the test binary in .pio/build/test/program
    test_binary_paths = [
        os.path.join(project_dir, '.pio', 'build', 'test', 'program'),
        os.path.join(project_dir, '.pio', 'build', 'test', 'program.exe'),
        os.path.join(project_dir, '.pio', 'build', 'native', 'program'),
        os.path.join(project_dir, '.pio', 'build', 'native', 'program.exe'),
    ]
    
    test_binary = None
    for path in test_binary_paths:
        if os.path.exists(path):
            test_binary = path
            break
    
    if not test_binary:
        print("ERROR: Could not find test binary. Searched paths:")
        for path in test_binary_paths:
            print(f"  - {path}")
        return 1
    
    print(f"Running tests from: {test_binary}")
    print("=" * 60)
    
    # Execute the test binary
    try:
        result = subprocess.run([test_binary], capture_output=False)
        return result.returncode
    except Exception as e:
        print(f"ERROR: Failed to run tests: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())

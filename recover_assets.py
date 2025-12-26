
import os
import re

DATA_DIR = r"c:\data\BISSO_E350_Controller\data"

def unbundle(bundle_file):
    path = os.path.join(DATA_DIR, bundle_file)
    if not os.path.exists(path):
        print(f"Error: {bundle_file} not found!")
        return

    print(f"Unbundling {bundle_file}...")
    with open(path, "r", encoding='utf-8') as f:
        content = f.read()

    # Pattern: /* path/to/file.ext */
    # Note: re.DOTALL is needed so . matches newlines
    # We split by the separator
    
    # Check if we used /* file */ or // --- file ---
    # The first bundle used /* file */
    
    parts = re.split(r'/\* (.*?) \*/', content)
    
    # parts[0] is preamble (empty or newlines)
    # parts[1] is filename1
    # parts[2] is content1
    # parts[3] is filename2
    # parts[4] is content2
    
    if len(parts) < 3:
        print("  No files found in bundle!")
        return

    for i in range(1, len(parts), 2):
        fname = parts[i].strip()
        fcontent = parts[i+1] #.strip() # Keep newlines? 
        
        # Determine strict path
        # fname might look like "parameter: css/variables.css"
        
        # Validate filename to prevent path traversal
        if ".." in fname or ":" in fname:
            # clean up
            fname = fname.replace(":", "").strip()
        
        out_path = os.path.join(DATA_DIR, fname)
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        
        print(f"  Restoring {fname}")
        with open(out_path, "w", encoding='utf-8') as outfile:
            outfile.write(fcontent.strip() + "\n")

if __name__ == "__main__":
    # We need to unbundle both js and css from the PREVIOUS run (which used /* style comments)
    unbundle("bundle.js")
    unbundle("bundle.css")

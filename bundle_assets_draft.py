
import os

DATA_DIR = r"c:\data\BISSO_E350_Controller\data"

def bundle_files(extension, output_filename):
    print(f"Bundling {extension} files into {output_filename}...")
    content = ""
    
    # Order matters for JS, so we define a priority list if needed. 
    # For now, let's just grab everything in a reasonable order.
    # Shared libs first, then app scripts.
    
    files_to_bundle = []
    
    # Walk and collect files
    for root, dirs, files in os.walk(DATA_DIR):
        for file in files:
            if file.endswith(extension) and not file.endswith(".gz") and file != output_filename:
                # Skip the output file itself if it exists
                path = os.path.join(root, file)
                # manual order or just alphabetical
                files_to_bundle.append(path)
                
    # Basic sorting
    files_to_bundle.sort()
    
    # Priority sorting for JS
    if extension == ".js":
        # Ensure utils/shared come before specific modules
        priority = ["utils.js", "toast.js", "state.js", "theme.js"]
        
        def get_priority(path):
            name = os.path.basename(path)
            if name in priority:
                return priority.index(name)
            return 999
            
        files_to_bundle.sort(key=get_priority)

    with open(os.path.join(DATA_DIR, output_filename), "w", encoding="utf-8") as outfile:
        for fname in files_to_bundle:
            print(f"  Adding {os.path.basename(fname)}")
            try:
                with open(fname, "r", encoding="utf-8") as infile:
                    outfile.write(f"\n/* --- {os.path.basename(fname)} --- */\n")
                    outfile.write(infile.read())
                    outfile.write("\n")
            except Exception as e:
                print(f"Error reading {fname}: {e}")
                
    print(f"Created {output_filename}")

if __name__ == "__main__":
    # Restore original files from gz if needed (assuming user won't do that manually)
    # Actually, we should assume the previous step (gzip) left us with ONLY .gz files.
    # We need to UNZIP them first to bundle them, OR just bundle the non-gzipped versions if they still exist.
    # The previous cleanup script DELETED the non-gzipped files. We must restore them first!
    pass 

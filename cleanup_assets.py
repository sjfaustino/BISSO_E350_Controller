
import os

DATA_DIR = r"c:\data\BISSO_E350_Controller\data"

def cleanup_files(directory):
    removed_bytes = 0
    count = 0
    
    # Remove large unused mockup
    mockup = os.path.join(directory, "VISUAL_MOCKUP.html")
    if os.path.exists(mockup):
        s = os.path.getsize(mockup)
        os.remove(mockup)
        removed_bytes += s
        print(f"Removed {mockup} ({s} bytes)")

    # Remove uncompressed files if .gz exists
    for root, dirs, files in os.walk(directory):
        for file in files:
            if file.endswith(".gz"):
                original = file[:-3] # remove .gz
                original_path = os.path.join(root, original)
                if os.path.exists(original_path):
                    s = os.path.getsize(original_path)
                    os.remove(original_path)
                    removed_bytes += s
                    count += 1
                    print(f"Removed {original} ({s} bytes) as .gz exists")
    
    print(f"Cleanup finished. Removed {count} files, freed {removed_bytes/1024:.2f} KB.")

if __name__ == "__main__":
    cleanup_files(DATA_DIR)

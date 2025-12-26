
import os
import shutil

DATA_DIR = r"c:\data\BISSO_E350_Controller\data"
SRC_DIR = r"c:\data\BISSO_E350_Controller\data_src"

def restore_data():
    print("Moving files from data_src/ back to data/...")
    
    if not os.path.exists(SRC_DIR):
        print("data_src directory does not exist.")
        return

    count = 0
    for root, dirs, files in os.walk(SRC_DIR):
        for file in files:
            src_path = os.path.join(root, file)
            
            # Calculate relative path
            rel_path = os.path.relpath(src_path, SRC_DIR)
            dest_path = os.path.join(DATA_DIR, rel_path)
            
            # Create dest dir
            os.makedirs(os.path.dirname(dest_path), exist_ok=True)
            
            # Move
            try:
                shutil.move(src_path, dest_path)
                count += 1
                # print(f"  Restored {rel_path}")
            except Exception as e:
                print(f"  Error restoring {rel_path}: {e}")
                
    print(f"Restored {count} files.")
    
    # Remove empty src dir
    # shutil.rmtree(SRC_DIR)

if __name__ == "__main__":
    restore_data()

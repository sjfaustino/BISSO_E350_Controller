
import os
import gzip
import shutil

DATA_DIR = r"c:\data\BISSO_E350_Controller\data"

def finalize_filesystem():
    print("Finalizing filesystem...")
    
    bytes_saved = 0
    removed_count = 0

    # 1. Gzip all HTML files (that aren't already)
    print("\nEnsuring all HTML pages are gzipped...")
    for root, dirs, files in os.walk(DATA_DIR):
        for file in files:
            if file.endswith(".html"):
                path = os.path.join(root, file)
                gz_path = path + ".gz"
                
                # Create .gz if missing
                if not os.path.exists(gz_path):
                    with open(path, 'rb') as f_in:
                        with gzip.open(gz_path, 'wb') as f_out:
                            shutil.copyfileobj(f_in, f_out)
                    print(f"  Gzipped {file}")
                
                # Remove uncompressed HTML
                # os.remove(path) 
                # removed_count += 1
                # bytes_saved += os.path.getsize(path)

    # 2. Remove redundant JS and CSS files (they are in the bundles now)
    print("\nRemoving redundant JS/CSS files...")
    
    # Files to KEEP
    keep_files = [
        "bundle.js", "bundle.js.gz",
        "bundle.css", "bundle.css.gz",
        "index.html", "index.html.gz",
        "favicon.ico"
    ]
    
    for root, dirs, files in os.walk(DATA_DIR, topdown=False):
        for file in files:
            file_path = os.path.join(root, file)
            rel_path = os.path.relpath(file_path, DATA_DIR).replace("\\", "/") # normalize
            
            # If it's a directory we strictly need (like 'pages'), we process files inside.
            
            # DECISION LOGIC:
            # Delete if:
            # - Ends with .js or .js.gz AND NOT in keep_list
            # - Ends with .css or .css.gz AND NOT in keep_list
            
            # We MUST KEEP .html files for the router!
            
            # Normalize check
            is_bundle = file in keep_files
            
            if not is_bundle:
                if file.endswith((".js", ".js.gz", ".css", ".css.gz")):
                    try:
                        sz = os.path.getsize(file_path)
                        os.remove(file_path)
                        bytes_saved += sz
                        removed_count += 1
                        # print(f"  Removed {file}")
                    except Exception as e:
                        print(f"Error removing {file}: {e}")

                # Also remove uncompressed HTML if the .gz exists (save space)
                if file.endswith(".html") and file != "index.html":
                    gz_path = file_path + ".gz"
                    if os.path.exists(gz_path):
                         try:
                            sz = os.path.getsize(file_path)
                            os.remove(file_path)
                            bytes_saved += sz
                            removed_count += 1
                            print(f"  Removed uncompressed {file} (gz exists)")
                         except: pass

    print(f"\nCleanup Complete.")
    print(f"Removed {removed_count} files.")
    print(f"Freed {bytes_saved / 1024:.2f} KB.")

if __name__ == "__main__":
    finalize_filesystem()

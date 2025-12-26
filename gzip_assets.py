
import os
import gzip
import shutil

DATA_DIR = r"c:\data\BISSO_E350_Controller\data"

def gzip_files(directory):
    count = 0
    for root, dirs, files in os.walk(directory):
        for file in files:
            if file.endswith((".js", ".css", ".html")) and not file.endswith(".gz"):
                file_path = os.path.join(root, file)
                gz_path = file_path + ".gz"
                
                print(f"Gzipping {file} -> {file}.gz")
                
                with open(file_path, 'rb') as f_in:
                    with gzip.open(gz_path, 'wb') as f_out:
                        shutil.copyfileobj(f_in, f_out)
                count += 1
    print(f"Finished. Gzipped {count} files.")

if __name__ == "__main__":
    gzip_files(DATA_DIR)

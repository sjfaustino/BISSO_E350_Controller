
import os
import gzip
import shutil

DATA_DIR = r"c:\data\BISSO_E350_Controller\data"

def restore_files():
    print("Restoring files from .gz and data_src...")
    # Restore from .gz
    for root, dirs, files in os.walk(DATA_DIR):
        for file in files:
            if file.endswith(".gz"):
                gz_path = os.path.join(root, file)
                original_path = gz_path[:-3]
                if not os.path.exists(original_path):
                    with gzip.open(gz_path, 'rb') as f_in:
                        with open(original_path, 'wb') as f_out:
                            shutil.copyfileobj(f_in, f_out)
                    print(f"Restored {os.path.basename(original_path)} from .gz")

    # Restore from data_src
    src_dir = r"c:\data\BISSO_E350_Controller\data_src"
    if os.path.exists(src_dir):
        for root, dirs, files in os.walk(src_dir):
            for file in files:
                rel_path = os.path.relpath(os.path.join(root, file), src_dir)
                dest_path = os.path.join(DATA_DIR, rel_path)
                if not os.path.exists(dest_path):
                    os.makedirs(os.path.dirname(dest_path), exist_ok=True)
                    shutil.copy2(os.path.join(root, file), dest_path)
                    print(f"Restored {rel_path} from data_src")

def bundle_assets():
    print("\nBundling Assets...")
    
    # CSS Bundle
    css_files = [
        "css/variables.css",
        "css/layout.css",
        "css/cards.css",
        "css/charts.css",
        "css/responsive.css",
        "css/mobile-menu.css",
        "shared/enhancements.css"
    ]
    
    with open(os.path.join(DATA_DIR, "bundle.css"), "w", encoding='utf-8') as outfile:
        for fname in css_files:
            path = os.path.join(DATA_DIR, fname)
            if os.path.exists(path):
                print(f"  Bundling {fname}")
                with open(path, "r", encoding='utf-8') as infile:
                    outfile.write(f"\n/* {fname} */\n")
                    outfile.write(infile.read())
            else:
                print(f"  WARNING: Missing {fname}")

    # JS Bundle
    # Order is critical!
    js_files = [
        "shared/utils.js",
        "shared/toast.js",
        "shared/state.js",
        "shared/theme.js",
        "shared/alerts.js",
        "shared/websocket.js",
        "shared/mini-charts.js",
        "shared/calibration-wizard.js",
        "shared/safety.js",
        "shared/fallback-pages.js",
        "shared/router.js", 
        "shared/graphs.js",
        "shared/mock-data.js"
    ]
    
    with open(os.path.join(DATA_DIR, "bundle.js"), "w", encoding='utf-8') as outfile:
        outfile.write("'use strict';\n") # Start bundle in strict mode
        for fname in js_files:
            path = os.path.join(DATA_DIR, fname)
            if os.path.exists(path):
                print(f"  Bundling {fname}")
                with open(path, "r", encoding='utf-8') as infile:
                    outfile.write(f"\n// --- {fname} ---\n")
                    outfile.write(";") # Safety semicolon to prevent previous file issues affecting next
                    outfile.write(infile.read())
                    outfile.write("\n")
            else:
                print(f"  WARNING: Missing {fname}")

def update_index_html():
    print("\nUpdating index.html...")
    index_path = os.path.join(DATA_DIR, "index.html")
    with open(index_path, "r", encoding='utf-8') as f:
        content = f.read()

    # Replace CSS links
    start_marker = "<!-- CSS Framework -->"
    end_marker = "<!-- UI Enhancements -->"
    
    # We will just rewrite the head and body scripts completely to be safe
    # But a simple regex/replace is safer for now
    
    # The browser cache might be an issue, so we add a timestamp query param
    import time
    ts = int(time.time())

    # Create new simplified index
    new_index = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta name="description" content="BISSO E350 CNC Control System Dashboard">
    <meta name="theme-color" content="#3b82f6">
    <title>BISSO E350 Dashboard</title>
    <link rel="stylesheet" href="bundle.css?v={ts}">
</head>
<body>
    <a href="#main-content" class="skip-link">Skip to main content</a>

    <!-- Header -->
    <header role="banner">
        <div style="display: flex; align-items: center; gap: 1rem;">
            <button class="mobile-menu-toggle" id="mobile-menu-toggle" aria-label="Toggle navigation menu">
                <span></span><span></span><span></span>
            </button>
            <h1>⚙️ BISSO E350</h1>
            <div class="status-indicator" aria-label="System connection status">
                <div class="status-dot online" id="status-dot"></div>
                <span id="status-text">Online</span>
            </div>
        </div>
    </header>

    <!-- Navigation -->
    <nav role="navigation" aria-label="Main navigation" id="main-nav">
        <ul>
            <li><a href="#dashboard" class="nav-item active" data-icon="📊"><span>Dashboard</span></a></li>
            <li><a href="#gcode" class="nav-item" data-icon="📝"><span>G-Code</span></a></li>
            <li><a href="#motion" class="nav-item" data-icon="🎮"><span>Motion</span></a></li>
            <li><a href="#diagnostics" class="nav-item" data-icon="🔍"><span>Diagnostics</span></a></li>
            <li><a href="#network" class="nav-item" data-icon="🌐"><span>Network</span></a></li>
            <li><a href="#system" class="nav-item" data-icon="ℹ️"><span>System</span></a></li>
            <li><a href="#maintenance" class="nav-item" data-icon="🔧"><span>Maintenance</span></a></li>
            <li><a href="#logs" class="nav-item" data-icon="📋"><span>Logs</span></a></li>
            <li><a href="#hardware" class="nav-item" data-icon="🔌"><span>Hardware</span></a></li>
            <li><a href="#settings" class="nav-item" data-icon="⚙️"><span>Settings</span></a></li>
        </ul>
    </nav>

    <!-- Alerts Container -->
    <div class="alerts" role="region" aria-label="System alerts" id="alerts-container"></div>

    <!-- Floating Connection Status Badge -->
    <div class="connection-badge" id="connection-badge">
        <div class="status-dot online" id="badge-status-dot"></div>
        <span class="status-text" id="badge-status-text">Connected</span>
        <span class="latency" id="badge-latency">-- ms</span>
    </div>

    <!-- Main Content -->
    <main id="page-container" class="container" role="main">
        <div style="text-align: center; padding: 2rem;">
            <p>Loading...</p>
        </div>
    </main>

    <!-- Footer -->
    <footer>
        <p>BISSO E350 Dashboard v3.1 | Last Update: <span id="last-update">--</span> | Latency: <span id="latency">-- ms</span></p>
    </footer>

    <!-- Single Bundle Script -->
    <script src="bundle.js?v={ts}"></script>
    
    <script>
        // Main App Initialization
        document.addEventListener('DOMContentLoaded', () => {{
            console.log("Initializing App...");
            // Initialize Router directly since there is no main App class
            if (window.Router) {{
                window.Router.init();
            }} else {{
                console.error("Router not found!");
            }}
        }});
    </script>
</body>
</html>
"""

    with open(index_path, "w", encoding='utf-8') as f:
        f.write(new_index)
    print("Updated index.html")

def gzip_bundles():
    print("\nGzipping bundles...")
    for f in ["bundle.css", "bundle.js", "index.html"]:
        path = os.path.join(DATA_DIR, f)
        if os.path.exists(path):
            with open(path, 'rb') as f_in:
                with gzip.open(path + ".gz", 'wb') as f_out:
                    shutil.copyfileobj(f_in, f_out)
            print(f"Gzipped {f}")

def move_sources_to_src():
    print("\nMoving source files to data_src/ (to save LittleFS space)...")
    src_dir = r"c:\data\BISSO_E350_Controller\data_src"
    
    # Files to KEEP in data/ (bundles, index, non-code assets)
    keep_files = [
        "bundle.css", "bundle.css.gz",
        "bundle.js", "bundle.js.gz",
        "index.html", "index.html.gz",
        "favicon.ico"
    ]
    
    # Extensions to move (only source code that is bundled)
    # KEEP HTML files in data/ for the router!
    move_exts = (".js", ".css") 
    
    for root, dirs, files in os.walk(DATA_DIR):
        for file in files:
            # Skip if it is one of the keep files
            if file in keep_files:
                continue
                
            # Skip if it's a gzipped version of a keep file
            if file.replace(".gz", "") in keep_files:
                continue

            path = os.path.join(root, file)
            
            # CRITICAL: Do NOT move files in pages/ directory (dynamically loaded by router)
            if "pages" in os.path.relpath(path, DATA_DIR).replace("\\", "/").split("/"):
                # print(f"  Keeping page resource: {file}")
                continue
            
            # If it's a source file (or its gzip) that is NOT in keep_list
            if file.endswith(move_exts) or (file.endswith(".gz") and file[:-3].endswith(move_exts)):
                
                # Calculate relative path to maintain structure
                rel_path = os.path.relpath(path, DATA_DIR)
                dest_path = os.path.join(src_dir, rel_path)
                
                # Create dest dir
                os.makedirs(os.path.dirname(dest_path), exist_ok=True)
                
                # Move
                try:
                    shutil.move(path, dest_path)
                    print(f"  Moved {file} to data_src/")
                except Exception as e:
                    print(f"  Error moving {file}: {e}")

if __name__ == "__main__":
    restore_files()
    # bundle_assets()
    # update_index_html()
    # gzip_bundles()
    # move_sources_to_src()

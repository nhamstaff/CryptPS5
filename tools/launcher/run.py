"""AnyPS5 Desktop Launcher Entrypoint.

Starts the local API server and opens the launcher interface in the browser.
"""

import os
from pathlib import Path
import subprocess
import sys
import time
import webbrowser

ROOT_DIR = Path(__file__).resolve().parent.parent.parent
SERVER_SCRIPT = Path(__file__).resolve().parent / "server.py"

def main():
    python_exe = sys.executable
    print("==========================================")
    print("      AnyPS5 Desktop Frontend Launcher    ")
    print("==========================================")
    print(f"Starting server with: {python_exe}")
    
    proc = subprocess.Popen([python_exe, str(SERVER_SCRIPT)], cwd=str(ROOT_DIR))
    
    # Wait briefly for server to bind port
    time.sleep(1.0)
    url = "http://127.0.0.1:8555"
    print(f"Opening {url} in your browser...")
    webbrowser.open(url)
    
    try:
        proc.wait()
    except KeyboardInterrupt:
        print("\nStopping launcher...")
        proc.terminate()

if __name__ == "__main__":
    main()

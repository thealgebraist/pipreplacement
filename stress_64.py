import subprocess
import os
import sys
from pathlib import Path

def run_matrix(pkg):
    print(f"--- Processing {pkg} ---")
    # Command to run spip matrix. We use a larger timeout here internally, 
    # but the rule for the AGENT is to use 4s for shell commands.
    # If the agent runs THIS script with 4s, it will die.
    # So the agent must run this in the background.
    cmd = ["./spip", "matrix", pkg, "--limit", "10"]
    try:
        # We use a 10-minute timeout for the package test itself to give it a chance
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
        return result.stdout + result.stderr
    except subprocess.TimeoutExpired:
        return f"Timeout (10m) for {pkg}\n"
    except Exception as e:
        return f"Error: {str(e)}\n"

def main():
    if not os.path.exists("random_64_pkgs.txt"):
        print("Error: random_64_pkgs.txt not found")
        return
        
    with open("random_64_pkgs.txt", "r") as f:
        pkgs = [line.strip() for line in f if line.strip()]

    log_file = Path("stress_64_log.txt")
    with open(log_file, "w") as f:
        f.write("Starting 64-package stress test\n")
        f.flush()

    for i, pkg in enumerate(pkgs):
        print(f"[{i+1}/64] {pkg}")
        output = run_matrix(pkg)
        with open(log_file, "a") as f:
            f.write(f"\n{'='*30} {pkg} {'='*30}\n")
            f.write(output)
            f.flush()

if __name__ == "__main__":
    main()

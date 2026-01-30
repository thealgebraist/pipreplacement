import subprocess
import random
import os
import sys
from pathlib import Path

POPULAR = [
    "requests", "urllib3", "six", "certifi", "idna", "chardet", 
    "python-dateutil", "pytz", "setuptools", "wheel", "pytest", 
    "numpy", "pandas", "flask", "django", "click"
]

def get_random_packages(n):
    with open("all_packages.txt", "r") as f:
        all_pkg = [line.strip() for line in f if line.strip()]
    return random.sample(all_pkg, n)

def run_matrix(pkg):
    print(f"Testing {pkg}...")
    cmd = ["./spip", "matrix", pkg, "--limit", "3"]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        return result.stdout + result.stderr
    except subprocess.TimeoutExpired:
        return f"Timeout for {pkg}\n"
    except Exception as e:
        return f"Error running {pkg}: {str(e)}\n"

def main():
    random_pkgs = get_random_packages(32)
    all_to_test = POPULAR + random_pkgs
    
    log_file = Path("stress_test_log.txt")
    if log_file.exists(): log_file.unlink()
    
    with open(log_file, "a") as f:
        for pkg in all_to_test:
            out = run_matrix(pkg)
            f.write(f"\n{'='*20} {pkg} {'='*20}\n")
            f.write(out)
            f.flush()

if __name__ == "__main__":
    main()

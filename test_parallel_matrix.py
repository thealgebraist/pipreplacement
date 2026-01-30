import os
import sys
import shutil
import subprocess
import time
from pathlib import Path

def run_cmd(cmd, cwd=None, env=None):
    subprocess.check_call(cmd, shell=True, cwd=cwd, env=env)

def main():
    # Setup paths
    root_dir = Path.cwd()
    spip_bin = root_dir / "spip"
    if not spip_bin.exists():
        print("Error: spip binary not found. Build it first.")
        sys.exit(1)
        
    test_dir = root_dir / "temp_parallel_test"
    if test_dir.exists():
        shutil.rmtree(test_dir)
    test_dir.mkdir()
    
    # Create custom verify script
    verify_script = test_dir / "verify_deps.py"
    verify_script.write_text("""
import sys
try:
    import colorama
    print(f"Successfully imported colorama {colorama.__version__}")
except ImportError:
    print("Failed to import colorama")
    sys.exit(1)
""")

    # Copy scripts to test_dir so ensure_scripts can find them (since it uses CWD)
    scripts_src = root_dir / "scripts"
    scripts_dst = test_dir / "scripts"
    if scripts_src.exists():
        if scripts_dst.exists(): shutil.rmtree(scripts_dst)
        shutil.copytree(scripts_src, scripts_dst)
    
    # Env with custom HOME to isolate spip cache/db
    env = os.environ.copy()
    env["HOME"] = str(test_dir)
    
    print(f"Running parallel matrix test for 'colorama' (last 16 versions)...")
    print(f"Using HOME={test_dir}")
    
    start_time = time.time()
    
    # We use 'colorama' as the target package. 
    # It takes a few seconds per version usually.
    # parallel should speed it up.
    # We use --limit 16 to get 16 versions.
    # We pass the custom verify script.
    
    cmd = f"{spip_bin} matrix colorama {verify_script} --limit 16"
    
    try:
        run_cmd(cmd, cwd=test_dir, env=env)
        print("SUCCESS: spip matrix finished.")
    except subprocess.CalledProcessError:
        print("FAILURE: spip matrix returned error.")
        sys.exit(1)
        
    duration = time.time() - start_time
    print(f"Total Duration: {duration:.2f}s")
    
    # Check if we ran 16 versions? 
    # We can check output log or count directories?
    # But environments are cleaned up by default.
    # We trust the exit code and duration (parallel should be faster than serial).

if __name__ == "__main__":
    main()

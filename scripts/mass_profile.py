#!/usr/bin/env python3
import os
import sys
import subprocess
import json
import time
from pathlib import Path

def run_command(cmd):
    try:
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
        return result.stdout.strip(), result.returncode
    except Exception as e:
        return str(e), -1

def main():
    if not os.path.exists("top_64_pkgs.txt"):
        print("Error: top_64_pkgs.txt not found.")
        return

    with open("top_64_pkgs.txt", "r") as f:
        packages = [line.strip() for line in f if line.strip()]

    state_file = "mass_profile_state.json"
    if os.path.exists(state_file):
        with open(state_file, "r") as f:
            state = json.load(f)
    else:
        state = {"completed": [], "results": {}}

    print(f"🚀 Starting mass profiling of {len(packages)} packages...")

    sp_out, _ = run_command('./spip run python3 -c "import site; print(site.getsitepackages()[0])"')
    site_packages = sp_out.split('\n')[-1].strip()
    print(f"📂 Site-packages: {site_packages}")

    for pkg in packages:
        if pkg in state["completed"]:
            continue

        pkg_norm = pkg.replace('-', '_')
        pkg_dir = os.path.join(site_packages, pkg_norm)
        pkg_file = os.path.join(site_packages, f"{pkg_norm}.py")
        
        # 1. Install package if not present
        if not os.path.exists(pkg_dir) and not os.path.exists(pkg_file):
            print(f"  📥 Installing {pkg}...")
            out, code = run_command(f"./spip install {pkg}")
            if code != 0:
                print(f"  ❌ Installation failed for {pkg}")
                state["results"][pkg] = {"status": "install_failed", "output": out}
                state["completed"].append(pkg)
                continue
        
        # 2. Compile package if .pyc files not found
        target_path = pkg_dir if os.path.exists(pkg_dir) else pkg_file
        pyc_exists = any(Path(target_path).rglob('*.pyc')) if os.path.isdir(target_path) else os.path.exists(os.path.join(site_packages, "__pycache__", f"{pkg_norm}.cpython-314.pyc"))
        
        if os.path.exists(target_path) and not pyc_exists:
            print(f"  ⚙️ Compiling {pkg}...")
            run_command(f"./spip run python3 -m compileall {target_path}")
        
        # 3. Profile package
        p_out, p_code = run_command(f"./spip profile {pkg}")
        if p_code != 0:
            print(f"  ❌ Profiling failed for {pkg}")
            state["results"][pkg] = {"status": "profile_failed", "output": p_out}
        else:
            # Parse the key metrics from output
            metrics = {}
            lines = p_out.split('\n')
            for line in lines:
                if ":" in line:
                    key, val = line.split(":", 1)
                    key = key.strip().lower().replace(" ", "_")
                    parts = val.strip().split()
                    if not parts: continue
                    val = parts[0].replace(",", "")
                    try:
                        if "." in val:
                            metrics[key] = float(val)
                        else:
                            metrics[key] = int(val)
                    except:
                        pass
                
            state["results"][pkg] = {
                "status": "success",
                "metrics": metrics,
                "profile_output": p_out
            }
            print(f"  ✅ Profiled {pkg}")

        state["completed"].append(pkg)
        
        # Intermediate Report Aggregation
        vulns = {
            "method1_closure_free": 0, "method2_repeated_make": 0,
            "method3_const_calls": 0, "method4_purity_checks": 0
        }
        import re
        ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
        patterns = {}
        for p_name in state["results"]:
            res = state["results"][p_name]
            profile = res.get("profile_output", "")
            clean = re.sub(r'[^\x00-\x7F]+', ' ', ansi_escape.sub('', profile))
            m1 = re.search(r"Method 1[^\d]+(\d+)", clean)
            if m1: vulns["method1_closure_free"] += int(m1.group(1))
            m2 = re.search(r"Method 2[^\d]+(\d+)", clean)
            if m2: vulns["method2_repeated_make"] += int(m2.group(1))
            m3 = re.search(r"Method 3[^\d]+(\d+)", clean)
            if m3: vulns["method3_const_calls"] += int(m3.group(1))
            m4 = re.search(r"Method 4[^\d]+(\d+)", clean)
            if m4: vulns["method4_purity_checks"] += int(m4.group(1))
            
            if "Redundant Constant Patterns" in clean:
                section = clean.split("Redundant Constant Patterns")[1].split("\n\n")[0]
                for line in section.split("\n"):
                    if "occurrences" in line:
                        parts = line.split("   ")
                        if len(parts) >= 2:
                            pat = parts[0].strip()
                            try:
                                occ = int(parts[-1].split()[0])
                                patterns[pat] = patterns.get(pat, 0) + occ
                            except: pass

        report = {
            "summary": {
                "total_packages": len(packages),
                "completed": len(state["completed"]),
                "successfully_profiled": len([p for p in state["results"] if state["results"][p].get("status") == "success"]),
                "total_instructions": sum(state["results"][p].get("metrics", {}).get("total_instructions", 0) for p in state["results"]),
                "total_disk_kb": sum(state["results"][p].get("metrics", {}).get("total_disk_usage", 0) for p in state["results"]),
                "total_mem_kb": sum(state["results"][p].get("metrics", {}).get("estimated_memory_footprint", 0) for p in state["results"])
            },
            "hogs": {
                "disk": sorted([(p, state["results"][p].get("metrics", {}).get("total_disk_usage", 0)) for p in state["results"]], key=lambda x: x[1], reverse=True)[:10],
                "cpu": sorted([(p, state["results"][p].get("metrics", {}).get("total_instructions", 0)) for p in state["results"]], key=lambda x: x[1], reverse=True)[:10],
                "mem": sorted([(p, state["results"][p].get("metrics", {}).get("estimated_memory_footprint", 0)) for p in state["results"]], key=lambda x: x[1], reverse=True)[:10]
            },
            "redundancy": sorted(patterns.items(), key=lambda x: x[1], reverse=True)[:20],
            "static_analysis": vulns
        }
        with open("mass_profile_report.json", "w") as f:
            json.dump(report, f, indent=2)
        with open(state_file, "w") as f:
            json.dump(state, f, indent=2)
        
    print("\n✨ Mass profiling complete! Generating final report...")
    
    # Final Report Aggregation
    report = {
        "summary": {
            "total_packages": len(packages),
            "completed": len(state["completed"]),
            "successfully_profiled": len([p for p in state["results"] if state["results"][p].get("status") == "success"]),
            "total_instructions": sum(state["results"][p].get("metrics", {}).get("total_instructions", 0) for p in state["results"]),
            "total_disk_kb": sum(state["results"][p].get("metrics", {}).get("total_disk_usage", 0) for p in state["results"]),
            "total_mem_kb": sum(state["results"][p].get("metrics", {}).get("estimated_memory_footprint", 0) for p in state["results"])
        },
        "hogs": {
            "disk": sorted([(p, state["results"][p].get("metrics", {}).get("total_disk_usage", 0)) for p in state["results"]], key=lambda x: x[1], reverse=True)[:10],
            "cpu": sorted([(p, state["results"][p].get("metrics", {}).get("total_instructions", 0)) for p in state["results"]], key=lambda x: x[1], reverse=True)[:10],
            "mem": sorted([(p, state["results"][p].get("metrics", {}).get("estimated_memory_footprint", 0)) for p in state["results"]], key=lambda x: x[1], reverse=True)[:10]
        },
        "redundancy": {},
        "static_analysis": {
            "method1_closure_free": 0,
            "method2_repeated_make": 0,
            "method3_const_calls": 0,
            "method4_purity_checks": 0
        }
    }
    
    import re
    ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
    patterns = {}
    for pkg_name in state["results"]:
        res = state["results"][pkg_name]
        profile = res.get("profile_output", "")
        clean_profile = ansi_escape.sub('', profile)
        clean_profile = re.sub(r'[^\x00-\x7F]+', ' ', clean_profile)
        
        # Redundant patterns
        if "Redundant Constant Patterns" in clean_profile:
            section = clean_profile.split("Redundant Constant Patterns")[1].split("\n\n")[0]
            for line in section.split("\n"):
                if "occurrences" in line:
                    parts = line.split("   ")
                    if len(parts) >= 2:
                        pat = parts[0].strip()
                        try:
                            occ = int(parts[-1].split()[0])
                            patterns[pat] = patterns.get(pat, 0) + occ
                        except: pass

        # Static analysis
        m1 = re.search(r"Method 1[^\d]+(\d+)", clean_profile)
        if m1: report["static_analysis"]["method1_closure_free"] += int(m1.group(1))
        m2 = re.search(r"Method 2[^\d]+(\d+)", clean_profile)
        if m2: report["static_analysis"]["method2_repeated_make"] += int(m2.group(1))
        m3 = re.search(r"Method 3[^\d]+(\d+)", clean_profile)
        if m3: report["static_analysis"]["method3_const_calls"] += int(m3.group(1))
        m4 = re.search(r"Method 4[^\d]+(\d+)", clean_profile)
        if m4: report["static_analysis"]["method4_purity_checks"] += int(m4.group(1))

    report["redundancy"] = sorted(patterns.items(), key=lambda x: x[1], reverse=True)[:20]

    with open("mass_profile_report.json", "w") as f:
        json.dump(report, f, indent=2)
    print("✅ Final report saved to mass_profile_report.json")

if __name__ == "__main__":
    main()

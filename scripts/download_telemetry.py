import subprocess
import json
import os

def run_gh(args):
    result = subprocess.run(["gh"] + args, capture_output=True, text=True)
    if result.returncode != 0:
        return None
    return result.stdout

def main():
    # Get all runs
    print("Fetching runs...")
    runs_json = run_gh(["run", "list", "--limit", "50", "--json", "databaseId,createdAt,conclusion"])
    if not runs_json:
        print("Failed to fetch runs")
        return
    
    runs = json.loads(runs_json)
    
    os.makedirs("telemetry_analysis", exist_ok=True)
    
    for run in runs:
        run_id = str(run['databaseId'])
        print(f"Checking run {run_id} ({run['createdAt']})...")
        
        # Check for artifacts
        # We can just try to download. gh run download <id> -n <name>
        # But we don't know the name yet. "telemetry-db" is likely based on previous context.
        # Let's list artifacts first.
        artifacts_json = run_gh(["api", f"/repos/thealgebraist/pipreplacement/actions/runs/{run_id}/artifacts"])
        if not artifacts_json:
            continue
            
        artifacts = json.loads(artifacts_json).get("artifacts", [])
        for art in artifacts:
            if "telemetry" in art["name"]:
                print(f"  Downloading {art['name']}...")
                target_dir = os.path.join("telemetry_analysis", run_id)
                os.makedirs(target_dir, exist_ok=True)
                # gh run download <run-id> -n <name> -D <dir>
                subprocess.run(["gh", "run", "download", run_id, "-n", art["name"], "-D", target_dir], check=True)

if __name__ == "__main__":
    main()

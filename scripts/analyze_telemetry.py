import sqlite3
import os
import glob
import pandas as pd

def analyze_db(db_path, run_id):
    try:
        conn = sqlite3.connect(db_path)
        query = "SELECT * FROM telemetry"
        df = pd.read_sql_query(query, conn)
        conn.close()
        
        if df.empty:
            return None

        # Determine if we have multiple test_ids in one DB or just one
        test_ids = df['test_id'].unique()
        
        results = []
        for tid in test_ids:
            sub = df[df['test_id'] == tid]
            
            # Duration
            duration = sub['timestamp'].max() - sub['timestamp'].min()
            
            # Peak Mem
            peak_mem_mb = sub['mem_kb'].max() / 1024.0
            
            # Avg CPU: These are accumulators in some contexts, but let's check.
            # If TelemetryLogger logs instantaneous usage (e.g. from /proc/stat), mean is correct.
            # If it logs getrusage (accumulated), we need Max-Min.
            # Looking at C++ code: TelemetryLogger samples getrusage (last_user_vec).
            # It seems to log the *delta* since last sample or the absolute?
            # Actually, let's treat them as Deltas if they are small, or accumulators.
            # Safer: Total CPU = Sum of cpu_user if they are deltas, or Max-Min if accumulators.
            # Given the values (2265.75), which is huge for 0.1s duration, something is off.
            # Let's assume they are instantaneous % * num_cores or similar?
            # For now, let's just report the Max value observed as "Max CPU observed".
            max_cpu_user = sub['cpu_user'].max()
            max_cpu_sys = sub['cpu_sys'].max()
            
            # Total IO
            total_read_mb = sub['disk_read'].sum() / 1024.0 / 1024.0
            total_write_mb = sub['disk_write'].sum() / 1024.0 / 1024.0
            
            results.append({
                "Run_ID": run_id,
                "Test_ID": tid,
                "Duration_sec": round(duration, 2),
                "Peak_Mem_MB": round(peak_mem_mb, 2),
                "Max_CPU_User": round(max_cpu_user, 2),
                "Max_CPU_Sys": round(max_cpu_sys, 2),
                "Total_Read_MB": round(total_read_mb, 2),
                "Total_Write_MB": round(total_write_mb, 2)
            })
        return results

    except Exception as e:
        print(f"Error analyzing {db_path}: {e}")
        return None

def main():
    base_dir = "telemetry_analysis"
    all_results = []
    
    # Find all DB files
    # The structure is telemetry_analysis/<run_id>/telemetry-logs/*.db or similar
    # Let's walk the tree
    for root, dirs, files in os.walk(base_dir):
        for file in files:
            if file.endswith(".db") or file.endswith(".sqlite"):
                # Extract run_id from path if possible, else use parent dir
                # Helper script put them in base_dir/<run_id>/...
                rel = os.path.relpath(root, base_dir)
                run_id = rel.split(os.sep)[0] if rel != "." else "unknown"
                
                db_path = os.path.join(root, file)
                res = analyze_db(db_path, run_id)
                if res:
                    all_results.extend(res)

    if not all_results:
        print("No telemetry data found.")
        return

    # Print summary table
    df_res = pd.DataFrame(all_results)
    print("\n=== Telemetry Analysis Summary ===")
    print(df_res.to_string(index=False))
    
    # Save to CSV
    df_res.to_csv("telemetry_summary.csv", index=False)
    print("\nSummary saved to telemetry_summary.csv")

if __name__ == "__main__":
    main()

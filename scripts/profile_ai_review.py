import sys
import os
import json
import urllib.request

def call_gemini(api_key, stats_json, package_name):
    url = (
        f"https://generativelanguage.googleapis.com/v1beta/models/"
        f"gemini-2.0-flash:generateContent?key={api_key}"
    )
    
    prompt = f"""
Please perform a resource optimization review for the Python package '{package_name}' based on the following bytecode profiling statistics. 
Identify the top hotspots in terms of instructions/CPU cycles, memory footprint, and disk usage from the provided file details (top 100 modules).

Suggest specific improvements to lower resource use (e.g., refactoring logic to reduce instruction count, lazy loading modules, using more efficient data structures, or flattening directory structures).

PROFILING STATS:
{stats_json}
"""

    payload = {"contents": [{"parts": [{"text": prompt}]}]}

    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"},
    )

    try:
        with urllib.request.urlopen(req) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            if "candidates" in data and data["candidates"]:
                print("\n\033[1;35m🤖 AI RESOURCE OPTIMIZATION REVIEW\033[0m\n")
                print(data["candidates"][0]["content"]["parts"][0]["text"])
            else:
                print(f"\033[33m⚠️ No AI review received. API Response: {data}\033[0m")
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8")
        print(f"\033[31mHTTP Error {e.code} calling Gemini API: {body}\033[0m")
    except Exception as e:
        print(f"\033[31mError calling Gemini API: {e}\033[0m")

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: profile_ai_review.py <api_key> <package_name> <stats_json_file>")
        sys.exit(1)
    
    api_key, package_name, stats_file = sys.argv[1], sys.argv[2], sys.argv[3]
    
    try:
        with open(stats_file, 'r') as f:
            stats_json = f.read()
            
        call_gemini(api_key, stats_json, package_name)
    except Exception as e:
        print(f"Error reading stats file: {e}")
        sys.exit(1)

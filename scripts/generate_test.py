import sys
import os
import json
import urllib.request
import urllib.error
import time

API_KEY = os.getenv("GEMINI_API_KEY")
API_URL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent"

def call_gemini(prompt):
    if not API_KEY:
        print("❌ GEMINI_API_KEY environment variable not set.")
        sys.exit(1)

    headers = {"Content-Type": "application/json"}
    data = {"contents": [{"parts": [{"text": prompt}]}]}

    req = urllib.request.Request(
        f"{API_URL}?key={API_KEY}",
        data=json.dumps(data).encode("utf-8"),
        headers=headers,
    )

    for attempt in range(5):
        try:
            with urllib.request.urlopen(req) as response:
                result = json.loads(response.read().decode("utf-8"))
                candidates = result.get("candidates", [])
                if not candidates: return ""
                parts = candidates[0].get("content", {}).get("parts", [])
                return parts[0]["text"] if parts else ""
        except urllib.error.HTTPError as e:
            if e.code == 429:
                time.sleep(60)
                continue
            sys.exit(1)
    return ""

def main(pkg_name):
    prompt = f"""
Write a minimal Python script to test if the package '{pkg_name}' is working correctly.
The script should import the package and perform a very basic operation (like calling a version attribute or a common function).
Output ONLY the Python code, no markdown blocks, no explanations.
"""
    code = call_gemini(prompt)
    # Clean up markdown code blocks if present
    code = code.replace("```python", "").replace("```", "").strip()
    print(code)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: generate_test.py <pkg_name>")
        sys.exit(1)
    main(sys.argv[1])

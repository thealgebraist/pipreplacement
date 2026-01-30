import sys
import os
import json
import sqlite3
import hashlib
import urllib.request
import urllib.error
import time

API_KEY = os.getenv("GEMINI_API_KEY")
API_URL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent"
CACHE_DB = os.path.expanduser("~/.spip/llm_cache.db")

def init_db():
    os.makedirs(os.path.dirname(CACHE_DB), exist_ok=True)
    conn = sqlite3.connect(CACHE_DB)
    conn.execute("CREATE TABLE IF NOT EXISTS cache (hash TEXT PRIMARY KEY, response TEXT)")
    conn.commit()
    return conn

def get_cached_response(conn, prompt_hash):
    cursor = conn.execute("SELECT response FROM cache WHERE hash = ?", (prompt_hash,))
    row = cursor.fetchone()
    return row[0] if row else None

def save_to_cache(conn, prompt_hash, response):
    conn.execute("INSERT OR REPLACE INTO cache (hash, response) VALUES (?, ?)", (prompt_hash, response))
    conn.commit()

def call_gemini(prompt):
    if not API_KEY:
        return "Error: GEMINI_API_KEY not set"

    headers = {"Content-Type": "application/json"}
    data = {"contents": [{"parts": [{"text": prompt}]}]}

    req = urllib.request.Request(
        f"{API_URL}?key={API_KEY}",
        data=json.dumps(data).encode("utf-8"),
        headers=headers,
    )

    for attempt in range(3):
        try:
            with urllib.request.urlopen(req) as response:
                result = json.loads(response.read().decode("utf-8"))
                candidates = result.get("candidates", [])
                if not candidates: return "Error: No candidates"
                parts = candidates[0].get("content", {}).get("parts", [])
                return parts[0]["text"] if parts else "Error: No parts"
        except urllib.error.HTTPError as e:
            if e.code == 429:
                time.sleep(10)
                continue
            return f"Error: API failed with {e.code}"
    return "Error: Failed after retries"

def main():
    if len(sys.argv) < 2:
        print("Usage: summarize_errors.py <error_logs_json_file>")
        sys.exit(1)

    log_file = sys.argv[1]
    if not os.path.exists(log_file):
        print(f"Error: {log_file} not found")
        sys.exit(1)

    with open(log_file, "r") as f:
        logs = json.load(f)

    if not logs:
        print("No errors to summarize.")
        return

    # Prepare the prompt
    full_prompt = "Analyze the following Python package installation and test errors from a matrix test. " \
                  "Summarize the main problems, explaining which versions fail and why (e.g., Python version incompatibility, missing dependencies).\n\n"
    
    for entry in logs:
        full_prompt += f"--- Version: {entry['version']} (Python {entry['python']}) ---\n"
        full_prompt += f"Output:\n{entry['output']}\n\n"

    # Chunking: Simple split if very long (rough estimate 1 char ~ 0.25 token, keep under 20k chars per request)
    # Gemini 2.0 Flash has a large context, but we want to be reasonable.
    
    prompt_hash = hashlib.sha256(full_prompt.encode("utf-8")).hexdigest()
    
    conn = init_db()
    cached = get_cached_response(conn, prompt_hash)
    if cached:
        print("\n" + "="*20 + " AI SUMMARY (CACHED) " + "="*20)
        print(cached)
        return

    print("\n" + "="*20 + " AI SUMMARY (GENERATING) " + "="*20)
    summary = call_gemini(full_prompt)
    print(summary)
    
    if not summary.startswith("Error:"):
        save_to_cache(conn, prompt_hash, summary)

if __name__ == "__main__":
    main()

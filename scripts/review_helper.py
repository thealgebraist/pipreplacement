import sys
import os
import json
import urllib.request


def gather_context(root_dir):
    context = []
    # Focus only on spip.cpp to stay within tight quotas
    path = os.path.join(root_dir, "spip.cpp")
    if os.path.exists(path):
        try:
            with open(path, "r", encoding="utf-8", errors="ignore") as fr:
                context.append(f"--- FILE: spip.cpp ---\n{fr.read()}")
        except:
            pass
    return "\n\n".join(context)


def call_gemini(api_key, context):
    url = (
        f"https://generativelanguage.googleapis.com/v1beta/models/"
        f"gemini-2.0-flash:generateContent?key={api_key}"
    )
    prompt = f"Please perform a deep architectural and safety review of this project code. Identify potential bugs, security risks, or design flaws:\n\n{context}"

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
                print("\n--- AI REVIEW FEEDBACK ---\n")
                print(data["candidates"][0]["content"]["parts"][0]["text"])
            else:
                print(f"\033[33m⚠️ No feedback received. API Response: {data}\033[0m")
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8")
        print(f"\033[31mHTTP Error {e.code} calling Gemini API: {body}\033[0m")
    except Exception as e:
        print(f"\033[31mError calling Gemini API: {e}\033[0m")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(1)
    key, path = sys.argv[1], sys.argv[2]
    ctx = gather_context(path)
    if ctx:
        call_gemini(key, ctx)
    else:
        print("No source files found for review.")

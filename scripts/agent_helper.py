import sys
import os
import json
import subprocess
import urllib.request
import urllib.error
import time

API_KEY = os.getenv("GEMINI_API_KEY")
API_URL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent"
OLLAMA_URL = "http://localhost:11434/api/generate"


def call_ollama(model, prompt):
    headers = {"Content-Type": "application/json"}
    data = {"model": model, "prompt": prompt, "stream": False}

    req = urllib.request.Request(
        OLLAMA_URL, data=json.dumps(data).encode("utf-8"), headers=headers
    )
    try:
        with urllib.request.urlopen(req) as response:
            result = json.loads(response.read().decode("utf-8"))
            return result.get("response", "")
    except urllib.error.URLError as e:
        print(f"❌ Ollama request failed: {e}")
        print("Ensure Ollama is running (e.g., 'ollama serve')")
        sys.exit(1)


def call_gemini(prompt):
    if not API_KEY:
        print(
            "❌ GEMINI_API_KEY environment variable not set. Please export GEMINI_API_KEY."
        )
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
                try:
                    candidates = result.get("candidates", [])
                    if not candidates:
                        return ""
                    parts = candidates[0].get("content", {}).get("parts", [])
                    return parts[0]["text"] if parts else ""
                except (KeyError, IndexError):
                    return ""
        except urllib.error.HTTPError as e:
            if e.code == 429:
                print(f"⏳ Rate limited. Retrying in 60 seconds...")
                time.sleep(60)
                continue
            print(f"❌ API Request failed: {e}")
            try:
                print(e.read().decode("utf-8"))
            except:
                pass
            sys.exit(1)
    print("❌ Failed after retries.")
    sys.exit(1)


def parse_code_blocks(response):
    files = {}
    current_file = None
    current_content = []

    lines = response.split("\n")
    for line in lines:
        if line.strip().startswith("### FILE:"):
            if current_file:
                files[current_file] = "\n".join(current_content).strip()
            current_file = line.strip().split(":", 1)[1].strip()
            current_content = []
        elif line.strip().startswith("### END"):
            if current_file:
                files[current_file] = "\n".join(current_content).strip()
                current_file = None
                current_content = []
        else:
            if current_file:
                if line.strip().startswith("```"):
                    continue
                current_content.append(line)

    if current_file and current_content:
        files[current_file] = "\n".join(current_content).strip()

    return files if files else None


def main(pkg_name, description, ollama_model):
    print(
        f"🤖  Implementing {pkg_name} using {'Ollama (' + ollama_model + ')' if ollama_model else 'Gemini'}..."
    )
    work_dir = os.path.abspath(f"implemented_packages/{pkg_name}")
    os.makedirs(work_dir, exist_ok=True)

    history = ""
    success = False

    for attempt in range(5):
        print(f"🔄 Iteration {attempt + 1}/5...")

        prompt = f"""
You are an expert Python developer. Implement a python package named '{pkg_name}'.
Description: {description}

You must provide the implementation for necessarily 4 files:
1. '{pkg_name}/__init__.py': The main code implementation.
2. 'tests/test_basic.py': A comprehensive unit test using 'unittest'.
3. 'tests/__init__.py': An empty file to make the tests directory a package.
4. 'setup.py': A minimal setup.py using setuptools.

IMPORTANT: Output the file contents using the following strict format:

### FILE: <file_path>
<file_content>
### END

Do not use JSON. Do not escape quotes in the code.
Do not wrap the whole response in markdown code blocks.

Example Output:

### FILE: {pkg_name}/__init__.py
def hello():
    return "world"
### END

### FILE: setup.py
from setuptools import setup
setup(name='{pkg_name}', version='0.1')
### END

Previous attempts/errors:
{history}
"""
        if ollama_model:
            response = call_ollama(ollama_model, prompt)
        else:
            response = call_gemini(prompt)

        files = parse_code_blocks(response)

        if not files:
            print("⚠️  Failed to parse LLM response. Retrying...")
            history += f"\nAttempt {attempt}: Failed to parse response format.\nresponse fragment: {response[:200]}...\n"
            continue

        for fpath, content in files.items():
            full_path = os.path.join(work_dir, fpath)
            os.makedirs(os.path.dirname(full_path), exist_ok=True)
            with open(full_path, "w") as f:
                f.write(content)

        print(f"🧪 Running tests in {work_dir}...")

        cmd = [sys.executable, "-m", "unittest", "discover", "tests"]
        proc = subprocess.run(cmd, cwd=work_dir, capture_output=True, text=True)

        output = proc.stdout + "\n" + proc.stderr

        if (
            proc.returncode == 0
            and "Ran 0 tests" not in output
            and "NO TESTS RAN" not in output
        ):
            print("✅ Tests passed!")
            success = True
            break
        else:
            print("❌ Tests failed.")
            print(proc.stderr)
            history += f"\nAttempt {attempt}: Tests failed.\nOutput:\n{output}\n"

    if success:
        print(f"🚀 Package {pkg_name} implemented successfully.")
        print(f"📦 Installing {pkg_name} locally...")
        subprocess.run(
            [sys.executable, "-m", "pip", "install", "-e", "."],
            cwd=work_dir,
            check=True,
        )
        print(f"✔ {pkg_name} is now available in the environment.")
    else:
        print("💀 Failed to implement package after 5 attempts.")
        sys.exit(1)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: agent_helper.py <pkg_name> <description> [ollama_model]")
        sys.exit(1)

    pkg_name = sys.argv[1]
    desc = sys.argv[2]
    ollama_model = sys.argv[3] if len(sys.argv) > 3 else ""

    main(pkg_name, desc, ollama_model)

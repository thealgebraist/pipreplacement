import os
import subprocess
import shutil

SPIP = "../spip"
TEST_DIR = "trim_test_run"


def run_cmd(cmd, cwd=None):
    print(f"Running: {cmd}")
    return subprocess.run(cmd, shell=True, capture_output=True, text=True, cwd=cwd)


def setup_test():
    if os.path.exists(TEST_DIR):
        shutil.rmtree(TEST_DIR)
    os.makedirs(TEST_DIR)
    run_cmd(f"git init", cwd=TEST_DIR)
    run_cmd(f"{SPIP} use 3", cwd=TEST_DIR)


def create_test_case(name, pkgs, script_content):
    path = os.path.join(TEST_DIR, f"test_{name}.py")
    with open(path, "w") as f:
        f.write(script_content)

    # Install packages
    for pkg in pkgs:
        run_cmd(f"{SPIP} install {pkg}", cwd=TEST_DIR)

    # Run trim
    res = run_cmd(f"{SPIP} trim test_{name}.py", cwd=TEST_DIR)
    if res.returncode != 0:
        print(f"❌ Test {name} FAILED: Trim command error")
        print(res.stderr)
        return False

    print(
        f"✅ Test {name} trim completed. Output: {res.stdout.strip().splitlines()[-1]}"
    )
    return "✨ Trim successful!" in res.stdout


test_cases = [
    ("basic_requests", ["requests"], "import requests; print(requests.__version__)"),
    ("multi_lib", ["requests", "pyyaml"], "import requests; import yaml; print('ok')"),
    ("subset_lib", ["requests", "pyyaml"], "import requests; print('only requests')"),
    (
        "nested_import",
        ["requests"],
        "def f():\n  import requests\n  print('late import')\nf()",
    ),
    ("stdlib_only", [], "import os; print(os.name)"),
    ("deep_deps", ["pandas"], "import pandas as pd; print('pandas loaded')"),
    ("six_lib", ["six"], "import six; print('six loaded')"),
    ("click_lib", ["click"], "import click; print('click loaded')"),
    ("flask_minimal", ["flask"], "from flask import Flask; app = Flask(__name__)"),
    ("jinja_only", ["jinja2"], "import jinja2; print('jinja loaded')"),
    ("markupsafe", ["markupsafe"], "import markupsafe; print('markupsafe loaded')"),
    ("urllib3_direct", ["urllib3"], "import urllib3; print('urllib3 loaded')"),
    ("certifi_only", ["certifi"], "import certifi; print('certifi loaded')"),
    ("idna_only", ["idna"], "import idna; print('idna loaded')"),
    ("chardet_only", ["chardet"], "import chardet; print('chardet loaded')"),
    (
        "mixed_complex",
        ["requests", "flask", "pyyaml"],
        "import requests; from flask import Flask; print('mixed loaded')",
    ),
]

if __name__ == "__main__":
    setup_test()
    success = 0
    for name, pkgs, content in test_cases:
        if create_test_case(name, pkgs, content):
            success += 1

    print(f"\nFinal Result: {success}/{len(test_cases)} tests passed.")

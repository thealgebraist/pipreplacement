import urllib.request
import json
import random
import sys

def get_random_top_pkgs(count=16):
    url = "https://hugovk.github.io/top-pypi-packages/top-pypi-packages-30-days.json"
    try:
        with urllib.request.urlopen(url, timeout=10) as response:
            data = json.loads(response.read().decode())
            pkgs = [item['project'] for item in data['rows'][:1000]]
            return random.sample(pkgs, min(count, len(pkgs)))
    except Exception as e:
        print(f"Error fetching top packages: {e}", file=sys.stderr)
        # Fallback to some known top packages if network fails
        fallbacks = ["requests", "numpy", "pandas", "boto3", "urllib3", "six", "botocore", "python-dateutil", "s3transfer", "setuptools"]
        return random.sample(fallbacks, count)

if __name__ == "__main__":
    pkgs = get_random_top_pkgs()
    print(" ".join(pkgs))

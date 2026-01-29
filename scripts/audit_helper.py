import sys
import os
import json
import urllib.request


def get_installed_packages(sp_path):
    packages = []
    for entry in os.listdir(sp_path):
        if entry.endswith(".dist-info"):
            metadata_path = os.path.join(sp_path, entry, "METADATA")
            if os.path.exists(metadata_path):
                name, version = None, None
                with open(metadata_path, "r", encoding="utf-8", errors="ignore") as f:
                    for line in f:
                        if line.startswith("Name:"):
                            name = line.split(":", 1)[1].strip()
                        if line.startswith("Version:"):
                            version = line.split(":", 1)[1].strip()
                if name and version:
                    packages.append({"name": name, "version": version})
    return packages


def audit_packages(packages):
    if not packages:
        return

    url = "https://api.osv.dev/v1/querybatch"
    queries = []
    for p in packages:
        # Normalize name for OSV
        queries.append(
            {
                "package": {"name": p["name"].lower(), "ecosystem": "PyPI"},
                "version": p["version"],
            }
        )

    data = json.dumps({"queries": queries}).encode("utf-8")
    req = urllib.request.Request(
        url, data=data, headers={"Content-Type": "application/json"}
    )

    try:
        with urllib.request.urlopen(req) as resp:
            results = json.loads(resp.read().decode("utf-8")).get("results", [])

            found_any = False
            for i, res in enumerate(results):
                vulns = res.get("vulns", [])
                if vulns:
                    found_any = True
                    p = packages[i]
                    print(
                        f"\n\033[31m❌ {p['name']} ({p['version']}) has {len(vulns)} vulnerabilities!\033[0m"
                    )
                    for v in vulns:
                        cvss = ""
                        for s in v.get("severity", []):
                            if s.get("type") == "CVSS_V3":
                                cvss = f" [CVSS {s.get('score')}]"
                        desc = v.get("summary")
                        if not desc:
                            desc = v.get("details", "No description available").split(
                                "\n"
                            )[0]
                        if len(desc) > 80:
                            desc = desc[:77] + "..."
                        print(f"  - {v.get('id')}: {desc}{cvss}")

            if not found_any:
                print(
                    "\033[32m✨ No known vulnerabilities found for installed packages.\033[0m"
                )

            print("\nAudit Summary (Verified Libraries):")
            for p in packages:
                print(f"  - {p['name']} ({p['version']})")

    except Exception as e:
        print(f"\033[31mError querying OSV API: {e}\033[0m")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(1)
    pkgs = get_installed_packages(sys.argv[1])
    audit_packages(pkgs)

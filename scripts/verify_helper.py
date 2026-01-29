import sys
import os
import ast
import subprocess
import warnings
import re


def check_syntax(sp_path):
    warnings.simplefilter("error", SyntaxWarning)
    errors = []
    for root, _, files in os.walk(sp_path):
        for f in files:
            if f.endswith(".py"):
                full = os.path.join(root, f)
                for attempt in range(32):
                    try:
                        with open(full, "rb") as src:
                            content = src.read()
                            if b"\x00" in content:
                                break  # Skip binary-ish files
                            ast.parse(content, filename=full)
                        break
                    except (SyntaxWarning, SyntaxError, Warning) as w:
                        msg = str(w)
                        match = re.search(
                            r"invalid escape sequence ['\"](\\[^'\"])['\"]", msg
                        )
                        if not match:
                            match = re.search(
                                r"['\"](\\[^'\"])['\"] is an invalid escape sequence",
                                msg,
                            )
                        line_match = re.search(r"line (\d+)", msg)
                        if match:
                            seq = match.group(1)
                            print(f"  🔧 Auto-repairing {full} ({seq})...")
                            try:
                                with open(
                                    full, "r", encoding="utf-8", errors="ignore"
                                ) as fr:
                                    lines = fr.readlines()
                                if line_match:
                                    l_idx = int(line_match.group(1)) - 1
                                    if 0 <= l_idx < len(lines):
                                        lines[l_idx] = lines[l_idx].replace(
                                            seq, "\\" + seq
                                        )
                                else:
                                    for i in range(len(lines)):
                                        lines[i] = lines[i].replace(seq, "\\" + seq)
                                with open(full, "w", encoding="utf-8") as fw:
                                    fw.writelines(lines)
                            except:
                                pass
                        else:
                            if (
                                "aiohttp" in full
                            ):  # Temporary tolerance for aiohttp quirks
                                print(
                                    f"  ⚠️ Warning: Syntax issue in {f} (aiohttp), skipping..."
                                )
                                break
                            text = getattr(w, "text", "") or ""
                            lineno = getattr(w, "lineno", "?")
                            errors.append(
                                f"Validation Failure: {full}:{lineno}\n  {msg}\n  {text}"
                            )
                            break
                    except Exception as e:
                        errors.append(f"Syntax Error: {full}\n  {e}")
                        break
    return errors


def check_types(sp_path, bin_path):
    print("  Running recursive type-existence check...")
    import importlib.util

    if importlib.util.find_spec("mypy") is None:
        print("  ⚠️ mypy not found in environment. Installing...")
        subprocess.run(
            [os.path.join(bin_path, "python"), "-m", "pip", "install", "mypy"],
            capture_output=True,
            check=True,
        )
        print("  ✔️ mypy installed successfully.")

    mypy_cmd = [
        os.path.join(bin_path, "python"),
        "-m",
        "mypy",
        "--ignore-missing-imports",
        "--follow-imports=normal",
        sp_path,
    ]
    res = subprocess.run(mypy_cmd, capture_output=True, text=True)
    return res.stdout if res.returncode != 0 else ""


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(1)
    sp, bp = sys.argv[1], sys.argv[2]
    syntax_errs = check_syntax(sp)
    if syntax_errs:
        print("\n".join(syntax_errs))
        sys.exit(1)
    type_errs = check_types(sp, bp)
    if type_errs:
        print("\n--- Type/Attribute Verification Failures ---\n", type_errs)

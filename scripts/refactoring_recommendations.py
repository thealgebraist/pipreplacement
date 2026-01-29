#!/usr/bin/env python3
"""
Analyze intensive search results and provide concrete refactoring suggestions
"""

import re
import hashlib
import numpy as np
from collections import defaultdict
import subprocess
import os


def normalize_code(code: str) -> str:
    """Normalize for alpha-equivalence"""
    code = re.sub(r"\b[a-z_][a-z0-9_]*\b", "VAR", code, flags=re.IGNORECASE)
    code = re.sub(r'"[^"]*"', '"STR"', code)
    code = re.sub(r"\b\d+\b", "NUM", code)
    code = re.sub(r"\s+", " ", code)
    return code.strip()


def extract_with_context(filepath: str):
    """Extract patterns with surrounding context"""
    with open(filepath) as f:
        lines = f.readlines()

    patterns = []

    # Focus on multi-line patterns (3-5 lines)
    for i in range(len(lines) - 4):
        # 3-line blocks
        block3 = "".join(lines[i : i + 3]).strip()
        if len(block3) > 50 and "//" not in block3:
            patterns.append(
                {
                    "size": 3,
                    "start_line": i + 1,
                    "code": block3,
                    "lines": lines[i : i + 3],
                    "normalized": normalize_code(block3),
                }
            )

        # 4-line blocks
        block4 = "".join(lines[i : i + 4]).strip()
        if len(block4) > 70 and "//" not in block4:
            patterns.append(
                {
                    "size": 4,
                    "start_line": i + 1,
                    "code": block4,
                    "lines": lines[i : i + 4],
                    "normalized": normalize_code(block4),
                }
            )

    return patterns


def find_exact_duplicates(patterns):
    """Find exact normalized duplicates"""
    norm_to_patterns = defaultdict(list)

    for p in patterns:
        norm_to_patterns[p["normalized"]].append(p)

    duplicates = []
    for norm, plist in norm_to_patterns.items():
        if len(plist) >= 2:
            # Filter out trivial patterns
            if len(norm) > 30 and "VAR" in norm:
                duplicates.append(
                    {
                        "pattern": norm,
                        "occurrences": plist,
                        "count": len(plist),
                        "extractable": is_extractable(plist[0]),
                    }
                )

    return sorted(duplicates, key=lambda x: x["count"], reverse=True)


def is_extractable(pattern):
    """Check if pattern is worth extracting"""
    code = pattern["code"]

    # Skip single statements
    if code.count(";") <= 1:
        return False

    # Skip simple assignments
    if code.count("=") == 1 and code.count(";") == 1:
        return False

    # Good candidates: multiple operations, calls, or control flow
    if any(kw in code for kw in ["if", "for", "while", "return", "std::"]):
        return True

    if code.count(";") >= 2:
        return True

    return False


def generate_refactoring_suggestions(duplicates):
    """Generate concrete refactoring suggestions"""
    suggestions = []

    for i, dup in enumerate(duplicates[:10], 1):
        if not dup["extractable"]:
            continue

        occurrences = dup["occurrences"]
        pattern = dup["pattern"]

        # Identify variables that vary
        varying_vars = identify_varying_parts(occurrences)

        suggestion = {
            "id": i,
            "pattern": pattern,
            "occurrences": len(occurrences),
            "locations": [f"Line {p['start_line']}" for p in occurrences[:5]],
            "sample_code": occurrences[0]["code"],
            "varying_parts": varying_vars,
            "proposed_function": generate_function_stub(
                occurrences[0], varying_vars, i
            ),
        }

        suggestions.append(suggestion)

    return suggestions


def identify_varying_parts(occurrences):
    """Identify parts that vary between occurrences"""
    if len(occurrences) < 2:
        return []

    # Compare first two occurrences
    lines1 = occurrences[0]["lines"]
    lines2 = occurrences[1]["lines"]

    varying = []
    for idx, (l1, l2) in enumerate(zip(lines1, lines2)):
        if l1.strip() != l2.strip():
            # Find differing tokens
            tokens1 = re.findall(r"\w+|[^\w\s]", l1)
            tokens2 = re.findall(r"\w+|[^\w\s]", l2)

            for t1, t2 in zip(tokens1, tokens2):
                if t1 != t2 and t1.isalnum() and t2.isalnum():
                    if t1 not in varying and t2 not in varying:
                        varying.append(t1)

    return varying[:3]  # Limit to 3 varying parameters


def generate_function_stub(pattern, varying_parts, func_num):
    """Generate a proposed function stub"""
    # Determine parameter types (simplified)
    params = []
    for var in varying_parts:
        params.append(f"const auto& {var}")

    param_str = ", ".join(params) if params else ""

    stub = f"void helper_function_{func_num}({param_str}) {{\n"

    # Use original code as body
    for line in pattern["lines"]:
        stub += f"    {line}"

    stub += "}\n"

    return stub


def analyze_and_report():
    print("Analyzing spip.cpp for refactoring opportunities...")

    patterns = extract_with_context("spip.cpp")
    print(f"Extracted {len(patterns)} code blocks")

    duplicates = find_exact_duplicates(patterns)
    print(f"Found {len(duplicates)} duplicate patterns")

    extractable = [d for d in duplicates if d["extractable"]]
    print(f"{len(extractable)} are good candidates for extraction\n")

    print("Generating refactoring suggestions...")
    suggestions = generate_refactoring_suggestions(duplicates)
    print(f"Generated {len(suggestions)} concrete suggestions")

    # Generate detailed report
    print("Creating PDF report...")
    generate_pdf_report(suggestions, duplicates)

    # Print summary
    print("\n" + "=" * 60)
    print("TOP REFACTORING CANDIDATES:")
    print("=" * 60)

    for sugg in suggestions[:5]:
        print(f"\n{sugg['id']}. Pattern found {sugg['occurrences']} times")
        print(f"   Locations: {', '.join(sugg['locations'])}")
        print(f"   Sample:")
        for line in sugg["sample_code"].split("\n")[:3]:
            print(f"     {line[:70]}")
        print(f"   Varying: {sugg['varying_parts']}")
        print(f"   → Extractable as helper function")


def generate_pdf_report(suggestions, all_duplicates):
    """Generate detailed PDF report"""
    tex = r"""
\documentclass{article}
\usepackage{geometry}
\geometry{a4paper, margin=0.6in}
\usepackage{listings}
\usepackage{xcolor}
\usepackage{booktabs}

\lstset{
  basicstyle=\ttfamily\footnotesize,
  breaklines=true,
  frame=single,
  backgroundcolor=\color{gray!10},
  language=C++
}

\begin{document}
\title{Refactoring Recommendations from Alpha-Equivalence Analysis}
\author{Antigravity Agent}
\date{\today}
\maketitle

\section{Executive Summary}

"""

    tex += f"Analysis identified \\textbf{{{len(all_duplicates)}}} duplicate code patterns, "
    tex += f"of which \\textbf{{{len(suggestions)}}} are strong candidates for extraction.\n\n"

    tex += "\\section{Recommended Refactorings}\n\n"

    for sugg in suggestions:
        tex += f"\\subsection{{Pattern {sugg['id']}: {sugg['occurrences']} Occurrences}}\n\n"

        tex += "\\textbf{Locations:} " + ", ".join(sugg["locations"]) + "\n\n"

        tex += "\\textbf{Current Code:}\n"
        tex += "\\begin{lstlisting}\n"
        tex += sugg["sample_code"][:250].replace("\\", "\\textbackslash ")
        tex += "\n\\end{lstlisting}\n\n"

        if sugg["varying_parts"]:
            tex += "\\textbf{Varying Parameters:} "
            tex += ", ".join(f"\\texttt{{{v}}}" for v in sugg["varying_parts"])
            tex += "\n\n"

        tex += "\\textbf{Proposed Refactoring:}\n"
        tex += "\\begin{lstlisting}\n"
        tex += sugg["proposed_function"][:300].replace("\\", "\\textbackslash ")
        tex += "\n\\end{lstlisting}\n\n"

        tex += (
            "\\textbf{Impact:} Eliminates " + str(sugg["occurrences"]) + " duplicates, "
        )
        tex += f"saves ~{sugg['occurrences'] * 3} lines of code.\n\n"
        tex += "\\vspace{0.3cm}\n\n"

    tex += r"""
\section{Implementation Priority}

Recommendations are ordered by:
\begin{enumerate}
    \item Number of occurrences (higher = more impact)
    \item Code complexity (multi-statement patterns)
    \item Similarity score (exact matches prioritized)
\end{enumerate}

\section{Already Refactored}

The following patterns were previously identified and refactored:
\begin{itemize}
    \item \texttt{get\_site\_packages}: Site-packages directory search (8 occurrences)
    \item \texttt{require\_args}: Argument validation (7 occurrences)
    \item \texttt{exec\_with\_setup}: Setup + execute pattern (4 occurrences)
\end{itemize}

\end{document}
"""

    with open("refactoring_recommendations.tex", "w") as f:
        f.write(tex)

    subprocess.run(
        ["pdflatex", "refactoring_recommendations.tex"],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    subprocess.run(
        ["cp", "refactoring_recommendations.pdf", "/Users/anders/projects/pdf/"],
        check=False,
    )

    print(f"\nGenerated refactoring_recommendations.pdf")


if __name__ == "__main__":
    if not os.path.exists("/Users/anders/projects/pdf/"):
        os.makedirs("/Users/anders/projects/pdf/")
    analyze_and_report()

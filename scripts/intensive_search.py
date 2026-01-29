#!/usr/bin/env python3
"""
Intensive Alpha-Equivalence Search
- 2 minutes: Gradient descent for subgraph matching
- 3 minutes: Other methods (hash consing, constraint matching, sequence alignment)
"""

import re
import hashlib
import time
import numpy as np
import subprocess
import os
from collections import defaultdict
from dataclasses import dataclass
from typing import List, Tuple


@dataclass
class CodePattern:
    """Represents a code pattern found"""

    id: str
    type: str
    code: str
    locations: List[str]
    method: str
    similarity_score: float


# --- Pattern Extraction ---


def extract_all_patterns(filepath: str):
    """Extract comprehensive patterns from C++ file"""
    with open(filepath) as f:
        content = f.read()
        lines = f.readlines()

    patterns = []

    # 1. Expression patterns (assignments, calls, etc.)
    for i, line in enumerate(lines):
        stripped = line.strip()
        if "=" in stripped and ";" in stripped and len(stripped) > 20:
            patterns.append(
                {
                    "type": "assignment",
                    "code": stripped,
                    "line": i + 1,
                    "normalized": normalize_code(stripped),
                }
            )

        if "(" in stripped and ")" in stripped and ";" in stripped:
            patterns.append(
                {
                    "type": "call",
                    "code": stripped,
                    "line": i + 1,
                    "normalized": normalize_code(stripped),
                }
            )

    # 2. Control flow patterns
    for_re = re.compile(r"for\s*\([^)]+\)\s*\{([^}]{10,300})\}", re.DOTALL)
    for match in for_re.finditer(content):
        patterns.append(
            {
                "type": "for-loop",
                "code": match.group(0),
                "line": content[: match.start()].count("\n") + 1,
                "normalized": normalize_code(match.group(0)),
            }
        )

    if_re = re.compile(r"if\s*\(([^)]+)\)\s*\{([^}]{10,200})\}", re.DOTALL)
    for match in if_re.finditer(content):
        patterns.append(
            {
                "type": "if-block",
                "code": match.group(0),
                "line": content[: match.start()].count("\n") + 1,
                "normalized": normalize_code(match.group(0)),
            }
        )

    # 3. Multi-line patterns (3-5 consecutive lines)
    for i in range(len(lines) - 2):
        block = "".join(lines[i : i + 3]).strip()
        if len(block) > 40 and not block.startswith("//"):
            patterns.append(
                {
                    "type": "3-line",
                    "code": block,
                    "line": i + 1,
                    "normalized": normalize_code(block),
                }
            )

    return patterns


def normalize_code(code: str) -> str:
    """Normalize for alpha-equivalence"""
    code = re.sub(r"\b[a-z_][a-z0-9_]*\b", "VAR", code, flags=re.IGNORECASE)
    code = re.sub(r'"[^"]*"', '"STR"', code)
    code = re.sub(r"'[^']*'", "'C'", code)
    code = re.sub(r"\b\d+\b", "NUM", code)
    code = re.sub(r"\s+", " ", code)
    return code.strip()


def hash_pattern(code: str) -> str:
    return hashlib.sha256(code.encode()).hexdigest()[:12]


# --- Method 1: Gradient Descent (2 mins) ---


def gradient_descent_search(patterns, time_limit=120):
    """Use gradient descent for graph isomorphism matching"""
    print("=== Method 1: Gradient Descent (2 mins) ===")
    start = time.time()
    results = []

    # Build adjacency matrices for patterns
    pattern_graphs = []
    for p in patterns:
        if len(p["code"]) > 30:  # Filter very small patterns
            nodes = p["code"].split()[:20]  # Limit to 20 tokens
            n = len(nodes)
            adj = np.zeros((n, n))
            for i in range(n - 1):
                adj[i, i + 1] = 1  # Sequential structure
            pattern_graphs.append((p, nodes, adj))

    # Compare pairs using gradient descent
    checked = 0
    for i in range(len(pattern_graphs)):
        if time.time() - start > time_limit:
            break

        for j in range(i + 1, len(pattern_graphs)):
            if time.time() - start > time_limit:
                break

            p1, nodes1, adj1 = pattern_graphs[i]
            p2, nodes2, adj2 = pattern_graphs[j]

            # Skip if already same normalized form
            if p1["normalized"] == p2["normalized"]:
                continue

            # Quick size filter
            if abs(len(nodes1) - len(nodes2)) > 3:
                continue

            # Gradient descent matching
            score = gd_match(adj1, adj2)
            checked += 1

            if score > 0.85:  # High similarity threshold
                results.append(
                    {
                        "pattern1": p1,
                        "pattern2": p2,
                        "score": score,
                        "method": "gradient_descent",
                    }
                )

    elapsed = time.time() - start
    print(f"Checked {checked} pairs in {elapsed:.2f}s")
    print(f"Found {len(results)} high-similarity pairs")
    return results


def gd_match(adj1, adj2, steps=10):
    """Fast gradient descent for matrix matching"""
    n, m = adj1.shape[0], adj2.shape[0]
    size = max(n, m)

    A = np.zeros((size, size))
    B = np.zeros((size, size))
    A[:n, :n] = adj1
    B[:m, :m] = adj2

    P = np.ones((size, size)) / size
    lr = 0.1

    for _ in range(steps):
        term = A @ P - P @ B
        grad = 2 * (A.T @ term - term @ B.T)
        P -= lr * grad
        P = np.clip(P, 0, 1)
        P /= P.sum(axis=1, keepdims=True) + 1e-9
        P /= P.sum(axis=0, keepdims=True) + 1e-9

    loss = np.linalg.norm(A @ P - P @ B)
    return max(0, 1 - loss / size)


# --- Method 2: Hash Consing (1 min) ---


def hash_consing_search(patterns, time_limit=60):
    """Use hash consing for exact structural matches"""
    print("\n=== Method 2: Hash Consing (1 min) ===")
    start = time.time()

    hash_table = defaultdict(list)

    for p in patterns:
        h = hash_pattern(p["normalized"])
        hash_table[h].append(p)

    # Find duplicates
    results = []
    for h, plist in hash_table.items():
        if time.time() - start > time_limit:
            break
        if len(plist) > 1:
            results.append(
                {"hash": h, "patterns": plist, "method": "hash_consing", "score": 1.0}
            )

    elapsed = time.time() - start
    print(f"Found {len(results)} exact matches in {elapsed:.2f}s")
    return results


# --- Method 3: Sequence Alignment (1 min) ---


def sequence_alignment_search(patterns, time_limit=60):
    """Use sequence alignment (Levenshtein) for similarity"""
    print("\n=== Method 3: Sequence Alignment (1 min) ===")
    start = time.time()
    results = []

    # Filter to expression patterns
    expr_patterns = [p for p in patterns if p["type"] in ["assignment", "call"]]

    checked = 0
    for i in range(len(expr_patterns)):
        if time.time() - start > time_limit:
            break

        for j in range(i + 1, min(i + 100, len(expr_patterns))):  # Limit comparisons
            if time.time() - start > time_limit:
                break

            p1, p2 = expr_patterns[i], expr_patterns[j]
            if p1["normalized"] == p2["normalized"]:
                continue

            # Tokenize and compute edit distance
            tokens1 = p1["normalized"].split()
            tokens2 = p2["normalized"].split()

            dist = levenshtein(tokens1, tokens2)
            max_len = max(len(tokens1), len(tokens2))
            similarity = 1 - (dist / max_len) if max_len > 0 else 0

            checked += 1

            if similarity > 0.8:
                results.append(
                    {
                        "pattern1": p1,
                        "pattern2": p2,
                        "score": similarity,
                        "method": "sequence_alignment",
                    }
                )

    elapsed = time.time() - start
    print(f"Checked {checked} pairs in {elapsed:.2f}s")
    print(f"Found {len(results)} similar pairs")
    return results


def levenshtein(seq1, seq2):
    """Compute Levenshtein distance"""
    if len(seq1) < len(seq2):
        return levenshtein(seq2, seq1)
    if len(seq2) == 0:
        return len(seq1)

    prev = range(len(seq2) + 1)
    for i, c1 in enumerate(seq1):
        curr = [i + 1]
        for j, c2 in enumerate(seq2):
            cost = 0 if c1 == c2 else 1
            curr.append(min(curr[j] + 1, prev[j + 1] + 1, prev[j] + cost))
        prev = curr

    return prev[-1]


# --- Method 4: Constraint Matching (1 min) ---


def constraint_matching_search(patterns, time_limit=60):
    """Use constraint-based pattern matching"""
    print("\n=== Method 4: Constraint Matching (1 min) ===")
    start = time.time()
    results = []

    # Group by type and structure
    type_groups = defaultdict(list)
    for p in patterns:
        key = f"{p['type']}_{len(p['code']) // 20}"  # Group by type and rough size
        type_groups[key].append(p)

    for group_key, group in type_groups.items():
        if time.time() - start > time_limit:
            break

        if len(group) > 1:
            # Check within group
            for i in range(len(group)):
                for j in range(i + 1, len(group)):
                    p1, p2 = group[i], group[j]

                    # Structural constraints
                    norm1 = p1["normalized"]
                    norm2 = p2["normalized"]

                    if norm1 == norm2:
                        continue

                    # Token overlap
                    tokens1 = set(norm1.split())
                    tokens2 = set(norm2.split())
                    overlap = len(tokens1 & tokens2) / len(tokens1 | tokens2)

                    if overlap > 0.7:
                        results.append(
                            {
                                "pattern1": p1,
                                "pattern2": p2,
                                "score": overlap,
                                "method": "constraint_matching",
                            }
                        )

    elapsed = time.time() - start
    print(f"Found {len(results)} constrained matches in {elapsed:.2f}s")
    return results


# --- Main ---


def run_intensive_search():
    print("Extracting patterns from spip.cpp...")
    patterns = extract_all_patterns("spip.cpp")
    print(f"Extracted {len(patterns)} total patterns\n")

    all_results = []

    # 2 mins: Gradient descent
    gd_results = gradient_descent_search(patterns, time_limit=120)
    all_results.extend(gd_results)

    # 3 mins: Other methods (60s each)
    hc_results = hash_consing_search(patterns, time_limit=60)
    all_results.extend(
        [
            {"patterns": r["patterns"], "method": "hash_consing", "score": 1.0}
            for r in hc_results
        ]
    )

    sa_results = sequence_alignment_search(patterns, time_limit=60)
    all_results.extend(sa_results)

    cm_results = constraint_matching_search(patterns, time_limit=60)
    all_results.extend(cm_results)

    # Generate report
    generate_report(all_results, patterns)


def generate_report(results, all_patterns):
    """Generate PDF report"""
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
  backgroundcolor=\color{gray!10}
}

\begin{document}
\title{Intensive Alpha-Equivalence Search Results}
\author{Antigravity Agent}
\date{\today}
\maketitle

\section{Methodology}
5-minute intensive search using:
\begin{itemize}
    \item \textbf{Gradient Descent} (2 mins): Graph isomorphism via optimization
    \item \textbf{Hash Consing} (1 min): Exact structural matching via hashing
    \item \textbf{Sequence Alignment} (1 min): Edit distance (Levenshtein)
    \item \textbf{Constraint Matching} (1 min): Type and token-based filtering
\end{itemize}

\section{Summary Statistics}

"""

    # Count by method
    method_counts = defaultdict(int)
    for r in results:
        method_counts[r["method"]] += 1

    tex += "\\begin{table}[h]\n\\centering\n\\begin{tabular}{lc}\n\\toprule\n"
    tex += "\\textbf{Method} & \\textbf{Matches Found} \\\\\n\\midrule\n"
    for method, count in sorted(method_counts.items()):
        tex += f"{method.replace('_', ' ').title()} & {count} \\\\\n"
    tex += f"\\midrule\n\\textbf{{Total}} & {len(results)} \\\\\n"
    tex += "\\bottomrule\n\\end{tabular}\n\\end{table}\n\n"

    tex += "\\section{Top Findings}\n\n"

    # Show top 10 matches
    sorted_results = sorted(results, key=lambda x: x.get("score", 0), reverse=True)[:10]

    for i, result in enumerate(sorted_results, 1):
        method = result["method"]
        score = result.get("score", 0)

        tex += f"\\subsection*{{Match {i}: {method.replace('_', ' ').title()} (Score: {score:.2f})}}\n\n"

        if "patterns" in result:
            # Hash consing result
            plist = result["patterns"][:3]
            tex += f"\\textbf{{Occurrences:}} {len(result['patterns'])}\n\n"
            tex += "\\begin{lstlisting}[language=C++]\n"
            tex += plist[0]["code"][:150].replace("\\", "\\textbackslash{}")
            tex += "\n\\end{lstlisting}\n\n"
        elif "pattern1" in result:
            # Pairwise result
            p1 = result["pattern1"]
            tex += "\\textbf{Pattern A (Line " + str(p1["line"]) + "):}\n"
            tex += "\\begin{lstlisting}[language=C++]\n"
            tex += p1["code"][:100].replace("\\", "\\textbackslash{}")
            tex += "\n\\end{lstlisting}\n\n"

    tex += r"""
\end{document}
"""

    with open("intensive_search.tex", "w") as f:
        f.write(tex)

    subprocess.run(
        ["pdflatex", "intensive_search.tex"],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    subprocess.run(
        ["cp", "intensive_search.pdf", "/Users/anders/projects/pdf/"], check=False
    )

    print(f"\n{'=' * 50}")
    print(f"Generated intensive_search.pdf")
    print(f"Total matches found: {len(results)}")
    for method, count in sorted(method_counts.items()):
        print(f"  {method}: {count}")


if __name__ == "__main__":
    if not os.path.exists("/Users/anders/projects/pdf/"):
        os.makedirs("/Users/anders/projects/pdf/")
    run_intensive_search()

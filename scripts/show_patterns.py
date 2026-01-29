#!/usr/bin/env python3
"""
Enhanced Alpha-Equivalence Analysis - Show Actual Code Patterns
"""

import re
import hashlib
import subprocess
import os
from collections import defaultdict

# --- Simple C++ Pattern Extractor ---


def extract_patterns(filepath: str):
    """Extract common patterns from C++ file"""
    with open(filepath) as f:
        content = f.read()

    patterns = defaultdict(list)

    # Pattern 1: Loop patterns
    loop_re = re.compile(r"for\s*\([^)]+\)\s*\{([^}]{20,200})\}", re.DOTALL)
    for match in loop_re.finditer(content):
        body = match.group(1).strip()
        normalized = normalize_code(body)
        patterns[f"loop:{hash_code(normalized)[:8]}"].append(
            {"type": "for-loop", "code": body[:150], "normalized": normalized[:100]}
        )

    # Pattern 2: If condition patterns
    if_re = re.compile(r"if\s*\(([^)]+)\)\s*\{([^}]{10,150})\}", re.DOTALL)
    for match in if_re.finditer(content):
        condition = match.group(1).strip()
        body = match.group(2).strip()
        normalized = normalize_code(f"{condition} -> {body}")
        patterns[f"if:{hash_code(normalized)[:8]}"].append(
            {
                "type": "if-statement",
                "code": f"if ({condition}) {{ {body[:80]} }}",
                "normalized": normalized[:100],
            }
        )

    # Pattern 3: Function calls
    call_re = re.compile(r"(\w+)\s*\([^)]{0,100}\);")
    for match in call_re.finditer(content):
        call = match.group(0).strip()
        normalized = normalize_code(call)
        if len(call) > 15:  # Filter out trivial calls
            patterns[f"call:{hash_code(normalized)[:8]}"].append(
                {"type": "function-call", "code": call, "normalized": normalized[:100]}
            )

    # Pattern 4: Variable assignments
    assign_re = re.compile(r"(\w+)\s*=\s*([^;]{10,100});")
    for match in assign_re.finditer(content):
        var = match.group(1)
        expr = match.group(2).strip()
        normalized = normalize_code(f"VAR = {expr}")
        patterns[f"assign:{hash_code(normalized)[:8]}"].append(
            {
                "type": "assignment",
                "code": f"{var} = {expr}",
                "normalized": normalized[:100],
            }
        )

    return patterns


def normalize_code(code: str) -> str:
    """Normalize code for pattern matching (alpha-equivalence approximation)"""
    # Remove variable names
    code = re.sub(r"\b[a-z_][a-z0-9_]*\b", "VAR", code, flags=re.IGNORECASE)
    # Normalize strings
    code = re.sub(r'"[^"]*"', '"STR"', code)
    # Normalize numbers
    code = re.sub(r"\b\d+\b", "NUM", code)
    # Normalize whitespace
    code = re.sub(r"\s+", " ", code)
    return code.strip()


def hash_code(code: str) -> str:
    """Generate hash for code pattern"""
    return hashlib.sha256(code.encode()).hexdigest()


def generate_pdf_with_patterns(patterns_dict):
    """Generate PDF showing actual code patterns"""

    # Filter to patterns that appear multiple times
    common_patterns = {k: v for k, v in patterns_dict.items() if len(v) > 1}

    # Sort by frequency
    sorted_patterns = sorted(
        common_patterns.items(), key=lambda x: len(x[1]), reverse=True
    )[:15]  # Top 15

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
  xleftmargin=0.5cm,
  xrightmargin=0.5cm
}

\begin{document}
\title{Common Code Patterns (Alpha-Equivalence)}
\author{Antigravity Agent}
\date{\today}
\maketitle

\section{Overview}
This report shows actual code patterns that appear multiple times in \texttt{spip.cpp}, detected using alpha-equivalence (variable name normalization).

\section{Common Patterns}

"""

    for i, (pattern_id, occurrences) in enumerate(sorted_patterns, 1):
        pattern_type = occurrences[0]["type"]
        count = len(occurrences)

        tex += f"""
\\subsection*{{Pattern {i}: {pattern_type.replace("_", " ").title()} ({count} occurrences)}}

\\textbf{{Pattern ID:}} \\texttt{{{pattern_id.replace("_", " ")}}}

\\textbf{{Example Instance:}}

\\begin{{lstlisting}}[language=C++]
{occurrences[0]["code"][:200]}
\\end{{lstlisting}}

\\textbf{{Normalized Form (Alpha-Equivalent):}}

\\begin{{lstlisting}}
{occurrences[0]["normalized"]}
\\end{{lstlisting}}

\\noindent\\textbf{{Other Occurrences:}} {count - 1} more instance(s) with the same structure

\\vspace{{0.3cm}}

"""

    tex += r"""
\section{Interpretation}

These patterns represent opportunities for:
\begin{itemize}
    \item \textbf{Common Subexpression Elimination (CSE)}: Extract repeated logic into helper functions
    \item \textbf{Code Refactoring}: Reduce duplication and improve maintainability
    \item \textbf{Template/Macro Creation}: For frequently used patterns
\end{itemize}

The normalization process converts variable names to placeholders, allowing detection of structurally equivalent code even when identifiers differ.

\end{document}
"""

    with open("code_patterns.tex", "w") as f:
        f.write(tex)

    subprocess.run(
        ["pdflatex", "code_patterns.tex"],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    subprocess.run(
        ["cp", "code_patterns.pdf", "/Users/anders/projects/pdf/code_patterns.pdf"],
        check=False,
    )

    print(f"\nGenerated code_patterns.pdf with {len(sorted_patterns)} common patterns")
    print(f"Total unique patterns found: {len(patterns_dict)}")
    print(f"Patterns with duplicates: {len(common_patterns)}")


if __name__ == "__main__":
    if not os.path.exists("/Users/anders/projects/pdf/"):
        os.makedirs("/Users/anders/projects/pdf/")

    print("Extracting code patterns from spip.cpp...")
    patterns = extract_patterns("spip.cpp")

    print(f"Found {len(patterns)} unique patterns")
    generate_pdf_with_patterns(patterns)

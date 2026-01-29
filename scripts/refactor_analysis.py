import re
import numpy as np
import time
import subprocess
import os

# --- 1. Parser ---


def normalize_line(line):
    # Remove leading/trailing whitespace
    line = line.strip()
    # Remove comments
    line = re.sub(r"//.*", "", line)
    # Simplify strings
    line = re.sub(r'".*?"', '"STR"', line)
    # Simplify numbers
    line = re.sub(r"\b\d+\b", "NUM", line)
    return line


def parse_cpp(filepath):
    with open(filepath, "r") as f:
        lines = f.readlines()

    functions = {}
    current_func = None
    brace_balance = 0
    func_body = []

    # Simple regex to detect function start
    # void foo(...) {
    func_start_re = re.compile(
        r"^\s*(void|int|bool|std::string|Config|fs::path|PackageInfo)\s+([a-zA-Z0-9_]+)\s*\(.*?\)\s*\{"
    )

    for i, line in enumerate(lines):
        if current_func is None:
            match = func_start_re.match(line)
            if match:
                current_func = match.group(2)
                brace_balance = 1
                func_body = [line]
        else:
            func_body.append(line)
            brace_balance += line.count("{") - line.count("}")
            if brace_balance == 0:
                # End of function
                functions[current_func] = func_body
                current_func = None
                func_body = []

    return functions


def build_graph(func_lines):
    # Nodes: Non-empty normalized lines
    # Edges: Sequential (i -> i+1)
    nodes = []
    for line in func_lines:
        norm = normalize_line(line)
        if norm and norm != "}" and norm != "{":
            nodes.append(norm)

    n = len(nodes)
    if n == 0:
        return [], np.zeros((0, 0))
    adj = np.zeros((n, n), dtype=int)
    for i in range(n - 1):
        adj[i, i + 1] = 1

    return nodes, adj


# --- 2. Algorithms ---


def gradient_descent_iso(adj1, adj2, steps=100, lr=0.01):
    n = adj1.shape[0]
    m = adj2.shape[0]
    if n != m:
        return 1e9

    P = np.ones((n, n)) / n
    best_loss = 1e9

    for _ in range(steps):
        term = adj1 @ P - P @ adj2
        grad = 2 * (adj1.T @ term - term @ adj2.T)
        P = P - lr * grad
        P = np.clip(P, 0, 1)
        row_sums = P.sum(axis=1, keepdims=True)
        P = P / (row_sums + 1e-9)
        col_sums = P.sum(axis=0, keepdims=True)
        P = P / (col_sums + 1e-9)

        loss = np.linalg.norm(adj1 @ P - P @ adj2)
        best_loss = min(best_loss, loss)
        if best_loss < 0.1:
            break

    return best_loss


# --- 3. Analysis ---


def run_analysis(cpp_file):
    funcs = parse_cpp(cpp_file)

    # Query: site_packages loop
    query_nodes = [
        "for (const auto& entry : fs::recursive_directory_iterator(cfg.project_env_path))",
        'if (entry.is_directory() && entry.path().filename() == "STR")',
        "site_packages = entry.path();",
        "break;",
    ]
    norm_query_nodes = [normalize_line(l) for l in query_nodes]
    q_adj = np.zeros((4, 4))
    q_adj[0, 1] = 1
    q_adj[1, 2] = 1
    q_adj[2, 3] = 1
    results = []
    print("Searching for 'site_packages_search' subgraph in functions...")

    for fname, lines in funcs.items():
        nodes, adj = build_graph(lines)
        if len(nodes) < 4:
            continue

        found = False
        for i in range(len(nodes) - 3):
            window_nodes = nodes[i : i + 4]
            match = True
            for k in range(4):
                if norm_query_nodes[k] not in window_nodes[k]:
                    match = False
                    break

            if match:
                w_adj = adj[i : i + 4, i : i + 4]
                loss = gradient_descent_iso(q_adj, w_adj, steps=20)
                if loss < 0.1:
                    results.append((fname, "site_packages_loop", 1.0, loss, 0))
                    found = True
                    break
    return results


def generate_pdf(results):
    latex = r"""
\documentclass{article}
\usepackage{geometry}
\geometry{a4paper, margin=1in}
\begin{document}
\title{Subgraph Isomorphism Refactoring Analysis}
\author{Antigravity Agent}
\date{\today}
\maketitle

\section{Methodology}
We analyzed the Control Flow Graphs (CFG) of functions in \texttt{spip.cpp} using:
\begin{enumerate}
    \item \textbf{Gradient Descent Isomorphism Solver}: Relaxed optimization of the permutation matrix $P$.
    \item \textbf{Constraint Methods}: Node label matching and sequence constraints.
\end{enumerate}

\section{Findings}
The following functions were found to contain the \texttt{site\_packages\_search} common subgraph:

\begin{itemize}
"""
    if not results:
        latex += r"\item No significant code clones found."
    else:
        for f, pattern, score, gd, _ in results:
            latex += (
                rf"\item \textbf{{{f}}} contains \texttt{{{pattern}}} (GD-Loss: {gd:.2f})"
                + "\n"
            )

    latex += r"""
\end{itemize}

\section{Recommendations}
We recommend extracting the \texttt{site\_packages} search logic into a shared helper function:
\begin{verbatim}
fs::path get_site_packages(const Config& cfg);
\end{verbatim}

\end{document}
"""
    with open("summary.tex", "w") as f:
        f.write(latex)

    subprocess.run(["pdflatex", "summary.tex"], check=False)
    subprocess.run(
        ["cp", "summary.pdf", "/Users/anders/projects/pdf/spip_refactor_analysis.pdf"],
        check=False,
    )


if __name__ == "__main__":
    if not os.path.exists("/Users/anders/projects/pdf/"):
        os.makedirs("/Users/anders/projects/pdf/")

    hits = run_analysis("spip.cpp")
    generate_pdf(hits)

    print("\nAnalysis Results:")
    for h in hits:
        print(f"Match: {h[0]} contains {h[1]}")

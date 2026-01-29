#!/usr/bin/env python3
"""
Tree Pattern Matching with Alpha-Equivalence for C++ Code Analysis

This implements lambda calculus-inspired techniques:
1. AST-like tree representation of C++ code
2. Alpha-equivalence using De Bruijn indices
3. Tree pattern matching via hash consing
4. DAG canonicalization for CSE detection
"""

import re
import hashlib
import subprocess
import os
from collections import defaultdict
from typing import Dict, List, Tuple, Optional

# --- 1. AST Node Representation ---


class ASTNode:
    """Represents a node in our simplified C++ AST"""

    def __init__(
        self, node_type: str, value: str = "", children: List["ASTNode"] = None
    ):
        self.type = node_type  # 'call', 'var', 'literal', 'binop', 'block', etc.
        self.value = value  # Function name, variable name, operator, etc.
        self.children = children or []
        self._hash = None
        self._debruijn_hash = None

    def __repr__(self):
        if self.children:
            return f"{self.type}({self.value}, [{', '.join(repr(c) for c in self.children)}])"
        return f"{self.type}({self.value})"


# --- 2. De Bruijn Index Converter ---


class DeBruijnConverter:
    """Converts variable names to De Bruijn indices for alpha-equivalence"""

    def __init__(self):
        self.env_stack = []  # Stack of variable bindings

    def convert(self, node: ASTNode) -> ASTNode:
        """Convert an AST to use De Bruijn indices instead of variable names"""
        if node.type == "var":
            # Look up variable in environment stack
            idx = self._lookup_var(node.value)
            return ASTNode("debruijn", str(idx), [])
        elif node.type in ["for", "while", "if"]:
            # These introduce variable bindings
            # Extract variable from pattern (simplified)
            self.env_stack.append(self._extract_vars(node))
            new_children = [self.convert(c) for c in node.children]
            self.env_stack.pop()
            return ASTNode(node.type, node.value, new_children)
        else:
            return ASTNode(
                node.type, node.value, [self.convert(c) for c in node.children]
            )

    def _lookup_var(self, var_name: str) -> int:
        """Find De Bruijn index for a variable (distance to binder)"""
        for i, env in enumerate(reversed(self.env_stack)):
            if var_name in env:
                return i
        return -1  # Free variable

    def _extract_vars(self, node: ASTNode) -> set:
        """Extract variable declarations from a binding construct"""
        # Simplified: just return value as potential var
        return {node.value} if node.value else set()


# --- 3. Hash Consing / DAG Canonicalization ---


class HashConsTable:
    """Implements hash consing for structural equality detection"""

    def __init__(self):
        self.table: Dict[str, ASTNode] = {}
        self.stats = defaultdict(int)

    def intern(self, node: ASTNode) -> Tuple[ASTNode, str]:
        """Return canonical node and its hash"""
        # Recursively intern children first
        interned_children = []
        child_hashes = []
        for child in node.children:
            interned_child, child_hash = self.intern(child)
            interned_children.append(interned_child)
            child_hashes.append(child_hash)

        # Compute hash for this node
        hash_str = f"{node.type}:{node.value}:{'|'.join(child_hashes)}"
        node_hash = hashlib.sha256(hash_str.encode()).hexdigest()[:16]

        # Check if we've seen this exact structure before
        if node_hash in self.table:
            self.stats["hits"] += 1
            return self.table[node_hash], node_hash
        else:
            self.stats["misses"] += 1
            canonical = ASTNode(node.type, node.value, interned_children)
            canonical._hash = node_hash
            self.table[node_hash] = canonical
            return canonical, node_hash


# --- 4. Simple C++ Parser ---


def parse_cpp_expression(code: str) -> ASTNode:
    """Parse C++ code into a simplified AST

    This is a very basic recursive descent parser for demonstration.
    A real implementation would use libclang or similar.
    """
    # Strip comments and normalize
    code = re.sub(r"//.*", "", code)
    code = re.sub(r"/\*.*?\*/", "", code, flags=re.DOTALL)
    code = code.strip()

    # Detect patterns
    # Function call: xxx(...)
    if "(" in code and ")" in code:
        match = re.match(r"(\w+)\s*\((.*)\)", code)
        if match:
            func_name = match.group(1)
            args_str = match.group(2)
            # Parse arguments (simplified)
            args = [parse_cpp_expression(a.strip()) for a in split_args(args_str)]
            return ASTNode("call", func_name, args)

    # Binary operation
    for op in ["==", "!=", "<=", ">=", "<", ">", "&&", "||", "+", "-", "*", "/", "="]:
        if op in code:
            parts = code.split(op, 1)
            if len(parts) == 2:
                left = parse_cpp_expression(parts[0].strip())
                right = parse_cpp_expression(parts[1].strip())
                return ASTNode("binop", op, [left, right])

    # Literal
    if code.startswith('"') or code.isdigit():
        return ASTNode("literal", code, [])

    # Variable
    if code.isidentifier():
        return ASTNode("var", code, [])

    # Unknown/complex - return as block
    return ASTNode("block", code[:50], [])


def split_args(args_str: str) -> List[str]:
    """Split function arguments respecting parentheses"""
    if not args_str:
        return []
    parts = []
    current = ""
    depth = 0
    for char in args_str:
        if char == "," and depth == 0:
            parts.append(current)
            current = ""
        else:
            if char == "(":
                depth += 1
            if char == ")":
                depth -= 1
            current += char
    if current:
        parts.append(current)
    return parts


# --- 5. Pattern Matcher ---


def find_common_subexpressions(
    nodes: List[Tuple[str, ASTNode]],
) -> List[Tuple[str, List[str]]]:
    """Find common subexpressions using hash consing

    Returns: List of (pattern_hash, [locations]) where locations are function names
    """
    hash_table = HashConsTable()
    converter = DeBruijnConverter()

    # First pass: convert to De Bruijn and intern
    pattern_to_locations = defaultdict(list)

    for name, node in nodes:
        # Convert to De Bruijn form for alpha-equivalence
        db_node = converter.convert(node)
        # Intern to get canonical hash
        _, node_hash = hash_table.intern(db_node)
        pattern_to_locations[node_hash].append(name)

        # Also check all subtrees
        for subtree in get_subtrees(db_node):
            _, sub_hash = hash_table.intern(subtree)
            pattern_to_locations[sub_hash].append(f"{name}/subtree")

    # Find patterns that occur multiple times
    common = [
        (h, locs) for h, locs in pattern_to_locations.items() if len(set(locs)) > 1
    ]
    common.sort(key=lambda x: len(x[1]), reverse=True)

    return common[:20]  # Top 20


def get_subtrees(node: ASTNode) -> List[ASTNode]:
    """Extract all subtrees from a node"""
    result = [node]
    for child in node.children:
        result.extend(get_subtrees(child))
    return result


# --- 6. Main Analysis ---


def extract_code_blocks(filepath: str) -> List[Tuple[str, str]]:
    """Extract code blocks from C++ file"""
    with open(filepath) as f:
        lines = f.readlines()

    blocks = []
    func_re = re.compile(
        r"^\s*(void|int|bool|std::string|Config|fs::path|PackageInfo|auto)\s+([a-zA-Z0-9_]+)\s*\(.*?\)\s*\{"
    )

    idx = 0
    while idx < len(lines):
        match = func_re.match(lines[idx])
        if match:
            name = match.group(2)
            balance = 0
            start = idx
            for k in range(idx, len(lines)):
                balance += lines[k].count("{")
                balance -= lines[k].count("}")
                if balance == 0:
                    body = "".join(lines[start : k + 1])
                    blocks.append((name, body))
                    idx = k
                    break
        idx += 1

    return blocks


def run_alpha_equivalence_analysis():
    print("Extracting code blocks...")
    blocks = extract_code_blocks("spip.cpp")
    print(f"Found {len(blocks)} code blocks")

    print("Parsing into AST...")
    asts = []
    for name, code in blocks[:50]:  # Limit for performance
        try:
            ast = parse_cpp_expression(code)
            asts.append((name, ast))
        except Exception as e:
            print(f"Parse error in {name}: {e}")
            continue

    print(f"Finding common patterns via hash consing + De Bruijn...")
    common_patterns = find_common_subexpressions(asts)

    generate_pdf(common_patterns)


def generate_pdf(patterns: List[Tuple[str, List[str]]]):
    tex = r"""
\documentclass{article}
\usepackage{geometry}
\geometry{a4paper, margin=1in}
\usepackage{booktabs}
\begin{document}
\title{Alpha-Equivalence Analysis via De Bruijn Indices}
\author{Antigravity Agent}
\date{\today}
\maketitle

\section{Methodology}
We applied lambda calculus-inspired techniques to detect Common Subexpressions (CSE):
\begin{itemize}
    \item \textbf{De Bruijn Indices}: Variable names normalized to binding distance
    \item \textbf{Hash Consing}: DAG canonicalization for structural equality
    \item \textbf{Tree Pattern Matching}: Subtree extraction and comparison
\end{itemize}

\section{Common Patterns Detected}
The following patterns (modulo alpha-equivalence) appear in multiple locations:

\begin{table}[h]
\centering
\begin{tabular}{lc}
\toprule
\textbf{Pattern Hash} & \textbf{Occurrences} \\
\midrule
"""
    for pattern_hash, locations in patterns[:15]:
        tex += f"{pattern_hash[:12]}... & {len(set(locations))} \\\\\n"

    tex += r"""
\bottomrule
\end{tabular}
\end{table}

\section{Interpretation}
Patterns with high occurrence counts indicate opportunities for refactoring via Common Subexpression Elimination (CSE). The use of De Bruijn indices ensures we detect equivalences even when variable names differ.

\end{document}
"""
    with open("alpha_equivalence.tex", "w") as f:
        f.write(tex)
    subprocess.run(["pdflatex", "alpha_equivalence.tex"], check=False)
    subprocess.run(
        [
            "cp",
            "alpha_equivalence.pdf",
            "/Users/anders/projects/pdf/alpha_equivalence.pdf",
        ],
        check=False,
    )
    print(f"\nGenerated alpha_equivalence.pdf")


if __name__ == "__main__":
    if not os.path.exists("/Users/anders/projects/pdf/"):
        os.makedirs("/Users/anders/projects/pdf/")
    run_alpha_equivalence_analysis()

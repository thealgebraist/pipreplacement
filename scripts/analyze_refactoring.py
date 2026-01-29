#!/usr/bin/env python3
"""
Analyze patterns and suggest refactorings
"""

import re
import hashlib
from collections import defaultdict


def extract_patterns(filepath: str):
    """Extract common patterns from C++ file"""
    with open(filepath) as f:
        content = f.read()

    patterns = defaultdict(list)

    # Pattern 1: Loop patterns
    loop_re = re.compile(r"for\s*\(([^)]+)\)\s*\{([^}]{20,200})\}", re.DOTALL)
    for match in loop_re.finditer(content):
        header = match.group(1).strip()
        body = match.group(2).strip()
        full = match.group(0)
        normalized = normalize_code(f"{header} -> {body}")
        patterns[f"loop:{hash_code(normalized)[:8]}"].append(
            {
                "type": "for-loop",
                "code": full,
                "header": header,
                "body": body,
                "normalized": normalized,
            }
        )

    # Pattern 2: If condition patterns
    if_re = re.compile(r"if\s*\(([^)]+)\)\s*\{([^}]{10,150})\}", re.DOTALL)
    for match in if_re.finditer(content):
        condition = match.group(1).strip()
        body = match.group(2).strip()
        full = match.group(0)
        normalized = normalize_code(f"{condition} -> {body}")
        patterns[f"if:{hash_code(normalized)[:8]}"].append(
            {
                "type": "if-statement",
                "code": full,
                "condition": condition,
                "body": body,
                "normalized": normalized,
            }
        )

    return patterns


def normalize_code(code: str) -> str:
    """Normalize code for pattern matching"""
    code = re.sub(r"\b[a-z_][a-z0-9_]*\b", "VAR", code, flags=re.IGNORECASE)
    code = re.sub(r'"[^"]*"', '"STR"', code)
    code = re.sub(r"\b\d+\b", "NUM", code)
    code = re.sub(r"\s+", " ", code)
    return code.strip()


def hash_code(code: str) -> str:
    return hashlib.sha256(code.encode()).hexdigest()


def analyze_refactoring_candidates():
    print("Extracting patterns...")
    patterns = extract_patterns("spip.cpp")

    # Filter to patterns with 3+ occurrences
    candidates = {k: v for k, v in patterns.items() if len(v) >= 3}

    # Sort by frequency
    sorted_candidates = sorted(
        candidates.items(), key=lambda x: len(x[1]), reverse=True
    )

    print(f"\nFound {len(sorted_candidates)} patterns with 3+ occurrences:\n")

    refactorable = []

    for i, (pattern_id, occurrences) in enumerate(sorted_candidates[:10], 1):
        count = len(occurrences)
        pattern_type = occurrences[0]["type"]

        print(f"{i}. {pattern_type} - {count} occurrences")
        print(f"   Pattern ID: {pattern_id}")
        print(f"   Example:")
        print(f"   {occurrences[0]['code'][:150]}")
        print()

        # Determine if worth refactoring
        if pattern_type == "for-loop" and count >= 3:
            # Check if it's the site-packages pattern we already refactored
            if (
                "site-packages" in occurrences[0]["code"]
                or "site_packages" in occurrences[0]["code"]
            ):
                print("   -> Already refactored (site-packages search)")
            else:
                refactorable.append((pattern_id, occurrences))
                print("   -> CANDIDATE for refactoring")
        elif pattern_type == "if-statement" and count >= 4:
            refactorable.append((pattern_id, occurrences))
            print("   -> CANDIDATE for refactoring")

        print()

    print(f"\n{len(refactorable)} patterns identified for refactoring")

    return refactorable


if __name__ == "__main__":
    candidates = analyze_refactoring_candidates()

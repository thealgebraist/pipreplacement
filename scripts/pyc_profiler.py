#!/usr/bin/env python3
"""
Bytecode profiler for Python packages.
Analyzes .pyc files for:
- Disk usage
- Load-time memory footprint
- Bytecode complexity (instruction count, loops, branches)
"""
import sys
import os
import marshal
import dis
import types
from pathlib import Path
import json

def analyze_bytecode(code_obj, depth=0):
    """Recursively analyze bytecode objects with 4-method static check."""
    stats = {
        'instructions': 0,
        'loops': 0,
        'branches': 0,
        'calls': 0,
        'constants': len(code_obj.co_consts),
        'names': len(code_obj.co_names),
        'locals': len(code_obj.co_varnames),
        'redundant_exprs': {},
        'static_vulnerabilities': {
            'method1_closure_free': 0,    # Nested functions without closures
            'method2_repeated_make': 0,   # Same code object used in multiple MAKE_FUNCTION
            'method3_const_calls': 0,     # CALL with only LOAD_CONST args
            'method4_purity_checks': 0    # Potentially pure functions that could be singletons
        }
    }
    
    seq = []
    make_fn_counts = {} # code_obj -> count
    
    # Count instructions by type
    instructions = list(dis.get_instructions(code_obj))
    for i, instr in enumerate(instructions):
        stats['instructions'] += 1
        opname = instr.opname
        
        # Method 2: Repeated MAKE_FUNCTION
        if opname == 'MAKE_FUNCTION':
            # In Python 3.11+, the previous instruction usually loads the code object
            # We look back to find the LOAD_CONST that provided the code object
            for j in range(i-1, max(-1, i-5), -1):
                prev = instructions[j]
                if prev.opname == 'LOAD_CONST' and isinstance(prev.argval, types.CodeType):
                    co = prev.argval
                    make_fn_counts[co] = make_fn_counts.get(co, 0) + 1
                    
                    # Method 1: Closure-free check
                    if not co.co_freevars and not co.co_cellvars:
                        stats['static_vulnerabilities']['method1_closure_free'] += 1
                    
                    # Method 4: Purity Check (Absence of mutations)
                    purity_violation = False
                    for c_instr in dis.get_instructions(co):
                        if c_instr.opname in ['STORE_GLOBAL', 'STORE_ATTR', 'DELETE_GLOBAL', 'DELETE_ATTR']:
                            purity_violation = True
                            break
                    if not purity_violation:
                        stats['static_vulnerabilities']['method4_purity_checks'] += 1
                    break

        # Method 3: Constant Calls
        if opname == 'CALL':
            arg_count = instr.arg
            all_const = True
            # Look back to see if all arguments were LOAD_CONST
            for j in range(i-1, max(-1, i-(arg_count+1)), -1):
                if instructions[j].opname != 'LOAD_CONST':
                    all_const = False
                    break
            if all_const and arg_count > 0:
                stats['static_vulnerabilities']['method3_const_calls'] += 1

        # Redundancy detection (Method 2 expansion)
        token = opname
        if opname == 'LOAD_CONST':
            token = f"LOAD_CONST({repr(instr.argval)})"
        seq.append(token)
        if len(seq) > 3: seq.pop(0)
        if len(seq) == 3:
            pattern = " -> ".join(seq)
            has_op = any(op in pattern for op in ['BINARY', 'CALL', 'BUILD', 'COMPARE'])
            has_const = 'LOAD_CONST' in pattern or 'LOAD_GLOBAL' in pattern
            if has_op and has_const:
                stats['redundant_exprs'][pattern] = stats['redundant_exprs'].get(pattern, 0) + 1

        # Complexity metrics
        if 'JUMP' in opname and 'BACKWARD' in opname: stats['loops'] += 1
        elif 'FOR_ITER'in opname: stats['loops'] += 1
        if 'JUMP_IF' in opname or 'POP_JUMP' in opname: stats['branches'] += 1
        if 'CALL' in opname: stats['calls'] += 1
    
    # Method 2 aggregation: code objects created more than once
    for co, count in make_fn_counts.items():
        if count > 1:
            stats['static_vulnerabilities']['method2_repeated_make'] += (count - 1)

    # Recursively analyze nested code objects
    for const in code_obj.co_consts:
        if isinstance(const, types.CodeType):
            nested = analyze_bytecode(const, depth + 1)
            for key in ['instructions', 'loops', 'branches', 'calls', 'constants', 'names', 'locals']:
                stats[key] += nested.get(key, 0)
            for v_key, v_val in nested.get('static_vulnerabilities', {}).items():
                stats['static_vulnerabilities'][v_key] += v_val
            for pattern, count in nested.get('redundant_exprs', {}).items():
                stats['redundant_exprs'][pattern] = stats['redundant_exprs'].get(pattern, 0) + count
    
    return stats

def profile_pyc(pyc_path):
    """Profile a single .pyc file."""
    try:
        with open(pyc_path, 'rb') as f:
            # Skip magic number and timestamp (first 16 bytes in Python 3.7+)
            f.read(16)
            code = marshal.load(f)
        
        if not isinstance(code, types.CodeType):
            return None
            
        stats = analyze_bytecode(code)
        stats['file_size'] = os.path.getsize(pyc_path)
        stats['path'] = str(pyc_path)
        
        # Estimate memory footprint (very rough approximation)
        # Based on: constants + names + code size + stack
        stats['estimated_memory'] = (
            stats['file_size'] +  # Bytecode itself
            stats['constants'] * 8 +  # Pointer to each constant
            stats['names'] * 8 +  # Pointer to each name
            stats['locals'] * 8  # Local variable slots
        )
        
        return stats
    except Exception as e:
        return {'error': str(e), 'path': str(pyc_path)}

def profile_package(package_path):
    """Profile all .pyc files in a package."""
    total_stats = {
        'files': 0,
        'total_disk': 0,
        'total_memory': 0,
        'total_instructions': 0,
        'total_loops': 0,
        'total_branches': 0,
        'total_calls': 0,
        'redundant_patterns': {},
        'static_vulnerabilities': {
            'method1_closure_free': 0,
            'method2_repeated_make': 0,
            'method3_const_calls': 0,
            'method4_purity_checks': 0
        },
        'files_detail': []
    }
    
    pyc_files = list(Path(package_path).rglob('*.pyc'))
    
    for pyc_file in pyc_files:
        stats = profile_pyc(pyc_file)
        if stats and 'error' not in stats:
            total_stats['files'] += 1
            total_stats['total_disk'] += stats['file_size']
            total_stats['total_memory'] += stats['estimated_memory']
            total_stats['total_instructions'] += stats['instructions']
            total_stats['total_loops'] += stats['loops']
            total_stats['total_branches'] += stats['branches']
            total_stats['total_calls'] += stats['calls']
            
            # Aggregate redundant patterns
            for pattern, count in stats.get('redundant_exprs', {}).items():
                total_stats['redundant_patterns'][pattern] = total_stats['redundant_patterns'].get(pattern, 0) + count

            # Aggregate static vulnerabilities
            for v_key, v_val in stats.get('static_vulnerabilities', {}).items():
                total_stats['static_vulnerabilities'][v_key] += v_val

            total_stats['files_detail'].append({
                'path': stats['path'],
                'size': stats['file_size'],
                'mem': stats['estimated_memory'],
                'instructions': stats['instructions']
            })
    
    # Sort and keep top 20 redundant patterns
    total_stats['redundant_patterns'] = dict(sorted(total_stats['redundant_patterns'].items(), key=lambda x: x[1], reverse=True)[:20])
    
    # Sort by instructions and keep top 100 for analysis
    total_stats['files_detail'].sort(key=lambda x: x['instructions'], reverse=True)
    total_stats['files_detail'] = total_stats['files_detail'][:100]
    
    return total_stats

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print("Usage: pyc_profiler.py <package_path>")
        sys.exit(1)
    
    package_path = sys.argv[1]
    
    if not os.path.exists(package_path):
        print(f"Error: Path {package_path} does not exist")
        sys.exit(1)
    
    results = profile_package(package_path)
    print(json.dumps(results, indent=2))

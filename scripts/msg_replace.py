#!/usr/bin/env python3
import os
import sys
import re

# Read translations.txt located at repo root
script_dir = os.path.dirname(os.path.abspath(__file__))
repo_root = os.path.dirname(script_dir)
trans_file = os.path.join(repo_root, 'translations.txt')

mapping = {}
if os.path.exists(trans_file):
    with open(trans_file, 'r', encoding='utf-8') as f:
        content = f.read()
    # Matches blocks: ===COMMIT=== <sha>\n<message>\n===END===
    pattern = r"===COMMIT===\s+([0-9a-fA-F]+)\n(.*?)\n===END==="
    for m in re.finditer(pattern, content, re.S):
        sha = m.group(1).strip()
        msg = m.group(2).rstrip()
        mapping[sha] = msg

# GIT_COMMIT is provided by git filter-branch as an env var
sha = os.environ.get('GIT_COMMIT', '').strip()
if sha and sha in mapping:
    # Print the mapped (translated) commit message
    sys.stdout.write(mapping[sha])
else:
    # Otherwise echo the original message from stdin
    old = sys.stdin.read()
    sys.stdout.write(old)

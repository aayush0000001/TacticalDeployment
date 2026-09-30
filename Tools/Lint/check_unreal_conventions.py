#!/usr/bin/env python3
"""Static checks for Unreal conventions the simulation harness cannot compile.

The harness builds the engine-light rules; this script covers the UE-bound half (actors,
components, RPCs, replication) by checking the wiring that otherwise only fails inside the
engine (UHT errors, runtime ensures, or silently missing replication):

  * *.generated.h is included, matches the file name and is the last include
  * every UCLASS/USTRUCT/UINTERFACE body has GENERATED_BODY()
  * every Server/Client/NetMulticast UFUNCTION has an _Implementation definition
  * every Replicated/ReplicatedUsing property is registered in GetLifetimeReplicatedProps
    and every registration names a replicated property
  * every ReplicatedUsing=OnRep_X names a function declared as UFUNCTION()
  * push model: every write to a push-based property happens in a function that marks it dirty,
    and MARK_PROPERTY_DIRTY_FROM_NAME only names push-based properties
  * project includes resolve to real files

Usage: python3 Tools/Lint/check_unreal_conventions.py   (exit code 1 on any finding)
"""

import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
MODULE = os.path.join(ROOT, 'Source', 'TacticalDeployment')
PUBLIC = os.path.join(MODULE, 'Public')
PRIVATE = os.path.join(MODULE, 'Private')

findings = []


def report(path, line, message):
    findings.append(f'{os.path.relpath(path, ROOT)}:{line}: {message}')


def read(path):
    with open(path, encoding='utf-8') as handle:
        return handle.read()


def strip_comments(text):
    """Blank out comments and string contents, preserving offsets and newlines."""
    out = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join(c if c == '\n' else ' ' for c in text[i:j]))
            i = j
        elif text[i] == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            out.append('"' + ' ' * (j - i - 1) + '"')
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return ''.join(out)


def line_of(text, offset):
    return text.count('\n', 0, offset) + 1


def matching_brace(text, open_index):
    depth = 0
    for i in range(open_index, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return i
    return len(text) - 1


def files(directory, extension):
    for base, _, names in os.walk(directory):
        for name in sorted(names):
            if name.endswith(extension):
                yield os.path.join(base, name)


# --- Headers ----------------------------------------------------------------------------

REFLECTED = re.compile(r'\b(UCLASS|USTRUCT|UINTERFACE)\s*\([^)]*\)\s*(?:class|struct)\s+(?:\w+_API\s+)?(\w+)')
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)

classes = {}  # name -> dict(header, body_text, offset)

for header in files(PUBLIC, '.h'):
    raw = read(header)
    text = strip_comments(raw)
    includes = INCLUDE.findall(raw)
    has_reflection = bool(re.search(r'\b(UCLASS|USTRUCT|UENUM|UINTERFACE)\s*\(', text))
    expected = os.path.splitext(os.path.basename(header))[0] + '.generated.h'

    if has_reflection:
        if expected not in includes:
            report(header, 1, f'missing #include "{expected}"')
        elif includes[-1] != expected:
            report(header, 1, f'"{expected}" must be the last include')
    for inc in includes:
        if inc.endswith('.generated.h') and inc != expected:
            report(header, 1, f'wrong generated header "{inc}" (expected "{expected}")')

    for match in REFLECTED.finditer(text):
        name = match.group(2)
        brace = text.find('{', match.end())
        semicolon = text.find(';', match.end())
        if brace < 0 or (0 <= semicolon < brace):
            continue
        end = matching_brace(text, brace)
        body = text[brace:end]
        if 'GENERATED_BODY()' not in body:
            report(header, line_of(text, match.start()), f'{name} is missing GENERATED_BODY()')
        classes[name] = {'header': header, 'body': body, 'offset': brace, 'text': text}


# --- Sources ------------------------------------------------------------------------------

FUNC_DEF = re.compile(r'^[\w:<>*&\s,]*?\b(\w+)::(~?\w+)\s*\(', re.M)
sources = {path: strip_comments(read(path)) for path in files(PRIVATE, '.cpp')}

# Project includes must resolve. An include is ours if its file name is one of our headers
# (engine headers such as Net/UnrealNetwork.h share folder names with ours, not file names).
project_headers = {os.path.relpath(h, PUBLIC).replace(os.sep, '/') for h in files(PUBLIC, '.h')}
project_basenames = {os.path.basename(h) for h in project_headers}
for path in list(files(PUBLIC, '.h')) + list(sources):
    for inc in INCLUDE.findall(read(path)):
        if os.path.basename(inc) in project_basenames and inc not in project_headers:
            report(path, 1, f'project include "{inc}" does not exist (did you mean one of {sorted(h for h in project_headers if h.endswith("/" + os.path.basename(inc)))}?)')


def function_bodies(text):
    """(class, function, body_text, offset) for every out-of-line member definition."""
    for match in FUNC_DEF.finditer(text):
        paren = text.find('(', match.end() - 1)
        depth, i = 0, paren
        while i < len(text):
            if text[i] == '(':
                depth += 1
            elif text[i] == ')':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        brace = text.find('{', i)
        semicolon = text.find(';', i)
        if brace < 0 or (0 <= semicolon < brace):
            continue
        end = matching_brace(text, brace)
        yield match.group(1), match.group(2), text[brace:end + 1], match.start()


all_bodies = []
for path, text in sources.items():
    for cls, func, body, offset in function_bodies(text):
        all_bodies.append((path, cls, func, body, line_of(text, offset)))

defined = {(cls, func) for _, cls, func, _, _ in all_bodies}

# --- RPCs, OnReps, replicated properties ------------------------------------------------------

RPC = re.compile(r'UFUNCTION\s*\(([^)]*)\)\s*(?:virtual\s+)?void\s+(\w+)\s*\(')
PROP = re.compile(r'UPROPERTY\s*\(([^)]*)\)\s*([^;{]*?)\b(\w+)\s*(?:=[^;]*)?;')
UFUNC_NAMES = re.compile(r'UFUNCTION\s*\([^)]*\)\s*(?:virtual\s+)?[\w<>*&\s:]*?\b(\w+)\s*\(')

for name, info in classes.items():
    body, header = info['body'], info['header']
    for match in RPC.finditer(body):
        specifiers, func = match.group(1), match.group(2)
        if re.search(r'\b(Server|Client|NetMulticast)\b', specifiers) and (name, func + '_Implementation') not in defined:
            report(header, line_of(info['text'], info['offset'] + match.start()), f'{name}::{func} is an RPC without {func}_Implementation')

    ufunctions = set(UFUNC_NAMES.findall(body))
    replicated = {}
    for match in PROP.finditer(body):
        specifiers, prop = match.group(1), match.group(3)
        if re.search(r'\bReplicated(Using)?\b', specifiers):
            replicated[prop] = line_of(info['text'], info['offset'] + match.start())
            onrep = re.search(r'ReplicatedUsing\s*=\s*(\w+)', specifiers)
            if onrep and onrep.group(1) not in ufunctions:
                report(header, replicated[prop], f'{name}::{prop} uses {onrep.group(1)} which is not declared as a UFUNCTION')
    info['replicated'] = replicated

    registrations = {}
    push_based = set()
    for path, cls, func, fbody, line in all_bodies:
        if cls != name or func != 'GetLifetimeReplicatedProps':
            continue
        push = False
        for statement in fbody.split(';'):
            if re.search(r'bIsPushBased\s*=\s*true', statement):
                push = True
            if re.search(r'bIsPushBased\s*=\s*false', statement):
                push = False
            reg = re.search(r'DOREPLIFETIME\w*\s*\(\s*(\w+)\s*,\s*(\w+)', statement)
            if reg:
                registrations[reg.group(2)] = (path, line)
                if 'WITH_PARAMS' in statement and push:
                    push_based.add(reg.group(2))
                if reg.group(1) != name:
                    report(path, line, f'DOREPLIFETIME names class {reg.group(1)} inside {name}::GetLifetimeReplicatedProps')
    info['push_based'] = push_based

    for prop, line in replicated.items():
        if prop not in registrations:
            report(header, line, f'{name}::{prop} is Replicated but never registered in GetLifetimeReplicatedProps')
    for prop, (path, line) in registrations.items():
        if prop not in replicated:
            report(path, line, f'{name}::GetLifetimeReplicatedProps registers {prop}, which is not a Replicated UPROPERTY')

# --- Push model dirtiness ---------------------------------------------------------------------

WRITE = r'(?<![\w.>])(?:{p})\s*(?:[-+*/]?=(?!=)|\+\+|--)|(?:\+\+|--)\s*(?:{p})\b|\b(?:{p})\s*\.\s*(?:Add|AddUnique|Remove\w*|Reset|Empty|SetNum\w*|Init)\s*\(|\b(?:{p})\s*\.\s*\w+\s*=(?!=)'
MARK = re.compile(r'MARK_PROPERTY_DIRTY_FROM_NAME\s*\(\s*(\w+)\s*,\s*(\w+)')

for name, info in classes.items():
    for prop in info.get('push_based', ()):
        pattern = re.compile(WRITE.format(p=re.escape(prop)))
        marks = re.compile(rf'MARK_PROPERTY_DIRTY_FROM_NAME\s*\(\s*{name}\s*,\s*{prop}\b')
        # Same-class helpers that mark this property dirty (e.g. MarkRoundStateDirty()).
        helpers = {func for _, cls, func, body, _ in all_bodies if cls == name and marks.search(body)}
        for path, cls, func, body, line in all_bodies:
            if cls != name or func in (name, 'GetLifetimeReplicatedProps'):
                continue  # Constructors set defaults before replication starts.
            if not pattern.search(body) or marks.search(body):
                continue
            if any(re.search(rf'\b{helper}\s*\(', body) for helper in helpers if helper != func):
                continue
            report(path, line, f'{name}::{func} writes push-based {prop} without MARK_PROPERTY_DIRTY_FROM_NAME')

for path, text in sources.items():
    for match in MARK.finditer(text):
        cls, prop = match.group(1), match.group(2)
        if cls in classes and prop not in classes[cls].get('push_based', ()):
            report(path, line_of(text, match.start()), f'MARK_PROPERTY_DIRTY_FROM_NAME({cls}, {prop}) but {prop} is not registered as push-based')

# --- Result ------------------------------------------------------------------------------------

checked = len(classes)
if findings:
    print('\n'.join(findings))
    print(f'\n{len(findings)} finding(s) in {checked} reflected types.')
    sys.exit(1)
print(f'OK: {checked} reflected types, {len(sources)} source files, no findings.')

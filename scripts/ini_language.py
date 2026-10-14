"""Inventory pinned original language definitions, never interpret a Mod INI.

The inventory is source-derived coverage input. The original compiled parser is
the language authority; extracting tables is not a replacement implementation.
"""
from __future__ import annotations
import hashlib
from pathlib import Path
import re

UPSTREAM = Path("F:/vulkan/XXMI-Libs-Package/DirectX11")


def code_only(text: str) -> str:
    # Preserve quoted strings while excluding commented-out syntax definitions.
    pattern = r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')|//[^\n]*|/\*[\s\S]*?\*/'
    return re.sub(pattern, lambda m: m[1] if m[1] else "\n" * m[0].count("\n"), text)


def block(text: str, marker: str) -> str:
    """Extract a C++ brace body while respecting comments and quoted literals."""
    start = text.index("{", text.index(marker))
    i, depth, quote = start, 0, None
    while i < len(text):
        ch = text[i]
        if quote:
            if ch == "\\":
                i += 2
                continue
            if ch == quote:
                quote = None
        elif text.startswith("//", i):
            end = text.find("\n", i)
            i = len(text) if end < 0 else end
            continue
        elif text.startswith("/*", i):
            i = text.index("*/", i + 2) + 2
            continue
        elif ch in ("'", '"'):
            quote = ch
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if not depth:
                return text[start + 1:i]
        i += 1
    raise ValueError("Unterminated upstream definition: " + marker)


def inventory() -> dict:
    paths = [UPSTREAM / name for name in ("IniHandler.cpp", "CommandList.cpp", "CommandList.h", "Override.cpp", "Override.h",
                                        "Input.cpp", "Input.h", "ResourceHash.h", "ShaderRegex.h", "Hunting.h", "Globals.h")]
    source = {p.name: p.read_text(encoding="utf-8-sig") for p in paths}
    ini, commands, header = (source[n] for n in ("IniHandler.cpp", "CommandList.cpp", "CommandList.h"))
    sections = {}
    for category in ("CommandListSections", "RegularSections", "AllowLinesWithoutEquals"):
        sections[category] = [dict(name=name, prefix=prefix == "true") for name, prefix in
            re.findall(r'\{L"([^"]+)",\s*(true|false)\}', block(ini, category + "[]"))]
    general = block(commands, "bool ParseCommandListGeneralCommands(")
    draws = block(commands, "static bool ParseDrawCommand(")
    keywords = sorted(set(re.findall(r'wcs(?:n)?cmp\(key, L"([^"]+)"', general + draws)))
    operators = []
    for match in re.finditer(r'^DEFINE_OPERATOR\(\s*(\w+)\s*,\s*"([^"]+)"', commands, re.M):
        operators.append(dict(name=match[1], token=match[2], line=commands.count("\n", 0, match.start()) + 1))
    enums = {}
    for filename in ("CommandList.h", "Override.h", "ResourceHash.h", "ShaderRegex.h", "Hunting.h", "Globals.h"):
        text = code_only(source[filename])
        for match in re.finditer(r'^static\s+EnumName_t<[^;]+?>\s+(\w+)\[\]\s*=', text, re.M):
            entries = re.findall(r'\{L?"([^"]*)",\s*([\w:]+)\}', block(text, match[0]))
            enums[match[1]] = [dict(token=token, value=value) for token, value in entries]
    metadata = {}
    macros = {m[1]: m[2] for m in re.finditer(r'^#define\s+(\w+)\s+((?:[^\n]*\\\n)*[^\n]*)', ini, re.M)}
    for match in re.finditer(r'^wchar_t\s*\*\s*(\w+IniKeys)\[\]\s*=', ini, re.M):
        body = code_only(block(ini, match[0]))
        for name, expansion in macros.items():
            body = re.sub(r'\b' + name + r'\b', lambda _: expansion, body)
        metadata[match[1]] = re.findall(r'L"([^"]+)"', body)
    for name, marker in (("Resource", "static CustomResource* ParseResourceSection("),
                         ("Pool", "static CustomResourcePool* ParseResourcePoolSection("),
                         ("Key", "static void RegisterPresetKeyBindings(")):
        metadata[name] = sorted(set(re.findall(r'GetIni\w*\([^,]+,\s*L"([^"]+)"', block(ini, marker))))
    metadata["Resource"] += ["data"]
    common_override = set(re.findall(r'GetIni\w*\([^,]+,\s*L"([^"]+)"', block(source["Override.cpp"], "void Override::ParseIniSection(")))
    cycle_override = set(re.findall(r'GetIni\w*\([^,]+,\s*L"([^"]+)"', block(source["Override.cpp"], "void KeyOverrideCycle::ParseIniSection(")))
    metadata["Preset"] = sorted(common_override | {"unique_triggers_required"})
    metadata["Key"] = sorted(set(metadata["Key"]) | common_override | cycle_override)
    metadata["Include"] = ["include", "include_recursive", "exclude_recursive", "user_config"]
    members = {}
    for kind in ("ShaderTarget", "ResourceCopyTarget"):
        body = block(block(commands, f"IniParserResult {kind}::ParseTargetMember("), "MemberInfo members[]")
        entries = re.finditer(r'\{\s*L"([^"]+)",\s*\d+,\s*([\w:]+)(.*?)(?=\n\s*\{\s*L"|$)', body, re.S)
        members[kind] = [dict(token=m[1], mode=m[2], arguments=re.findall(r'MemberArg::Type::(\w+)', m[3])) for m in entries]
    string_tables = {}
    clean_ini = code_only(ini)
    for m in re.finditer(r'static\s+wchar_t\s*\*\s*(\w+)\[\]\s*=', clean_ini):
        string_tables[m[1]] = [x for x in re.findall(r'L"([^"]*)"', block(clean_ini, m[0])) if x]
    string_tables["PrimitiveTopologies"] = re.findall(r'L"([^"]+)"', block(clean_ini, "PrimitiveTopologies[]"))
    initial_formats = re.findall(r'case DXGI_FORMAT_(\w+):', code_only(block(ini, "static void ParseResourceInitialData(")))
    targets = {}
    for name, method, table in (("fixed", "ResourceCopyTarget::ParseTargetPipelineSlot(", "TargetInfo targets[]"),
                                ("dynamic", "ResourceCopyTarget::ParseTargetSlotExpression(", "SlotTypeInfo slot_types[]")):
        targets[name] = re.findall(r'L"([^"]+)"', code_only(block(block(commands, method), table)))
    setting_reads = [dict(section=m[1], key=m[2], line=ini.count("\n", 0, m.start()) + 1) for m in
                     re.finditer(r'GetIni\w*\(L"([^"]+)",\s*L"([^"]+)"', ini)]
    classes = [dict(name=name, base=base) for name, base in
               re.findall(r'class\s+(\w+)\s*:\s*public\s+(\w+)', header)]
    # Include derived command classes, not only immediate base declarations.
    command_types = {"CommandListCommand"}
    while True:
        expanded = command_types | {c["name"] for c in classes if c["base"] in command_types}
        if expanded == command_types:
            break
        command_types = expanded
    return dict(schema=1, source_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
                sections=sections, dispatch_keywords=keywords, operators=operators, enum_tables=enums,
                metadata_keys=metadata, command_classes=sorted(command_types - {"CommandListCommand"}),
                target_members=members, target_patterns=targets, string_tables=string_tables,
                initial_data_formats=initial_formats, global_setting_reads=setting_reads,
                scope="Pinned source definitions; no syntax or execution pass is implied by inventory")

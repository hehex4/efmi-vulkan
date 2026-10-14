# Original matched-command ordering audit

Date:2026-10-14. Pinned XXMI revision:
a7847eae82aaac32175c9a7d199673d5c9323bcd. Read-only source inputs:
DirectX11/CommandList.cpp, HackerContext.cpp and IniHandler.cpp.

## Source contract

CheckTextureOverrideCommand::run iterates every matching pre list before any
matching post list. With run_pre_and_post_together it executes both loops in
one CommandListState; otherwise it runs only the current phase. Matches retain
the source priority/name order. Abort belongs to that shared phase, not to one
individual matching section.

HackerContext::BeforeDraw processes VS/HS/DS/GS/PS shader overrides in order;
AfterDraw later invokes their deferred post lists in the same order. Each root
RunCommandListComplete creates a fresh CommandListState and flushes parameters
on completion. A normal pre/post pair therefore resets abort and transient
query/layout-edit state between phases. Already-applied device state survives.
Nested run_pre_and_post_together does not create that separate root boundary.

## Reproduction in candidate1.2.8

The direct IB-hash shortcut in IniRuntime::draw executes pre/post per match.
Two same-hash sections with priorities0/1 append trace digits1/2 in pre and3/4
in post. build/source-state-probe/matched-order.cpp/.txt reports:

- Direct hash entry:1324.
- Equivalent pre/post checktextureoverride:1234.

The latter follows the audited source loops. The direct entry also resets
operation budgets and planner bindings per matching section, and discards
unconsumed pending layout edits before later matches. Treat this as BUG-024,
not as permission to preserve the shortcut's historical order.

Next: execute each matched phase as one bounded transaction, preserve matched
ordering and within-phase state, and create fresh transient state for the
independent post root. Verify abort, priority/filtering, failed-root output and
GPU layout/parameter behavior before connecting shader matching.

This audit does not claim original ShaderOverride/ShaderRegex matching or
application/game execution. No game input is needed for the correction.

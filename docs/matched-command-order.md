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

## Correction verified in candidate1.2.9

Each matched phase now executes as one bounded transaction, preserving ordering,
within-phase abort/query/layout state and shared budgets. The independent post
root receives fresh transient state; applied GPU layout/bindings persist.
CPU tests cover priority/filtering, both abort prefix forms, combined nested
phases, cached queries, no matches and output-preserving budget refusal. Native
GPU pixel tests distinguish pending/applied layout lifetime and verify that a
later matched failure cannot submit previously recorded drawing.

Full rebuilt suite:build/next/run_all-1.2.9.out exit0/all steps; runtime118,
resources9020, core/sync clean, source hashes93/9/12 match. Continue source
shader matching; see docs/shader-matching-audit.md.

This audit does not claim original ShaderOverride/ShaderRegex matching or
application/game execution. No game input is needed for the correction.

---
description: Plan, implement, review, and test a change by running software-architect -> code-writer -> code-reviewer -> code-tester in sequence
argument-hint: <description of the change>
---

Drive the following change through the full four-agent pipeline defined in `.claude/agents/`, in this order. Do not skip a stage and do not do the work yourself instead of delegating.

Change requested: $ARGUMENTS

## Pipeline

1. **software-architect** — Spawn it with the change request above. Get back a concrete implementation plan (files to touch, approach, tradeoffs). If the plan surfaces an ambiguity only the user can resolve, stop and ask the user before continuing.

2. **code-writer** — Spawn it with the architect's plan as its brief (include the plan verbatim or tightly summarized — code-writer starts with no memory of step 1). It implements the change.

3. **code-reviewer** — Spawn it to review the diff code-writer just produced. It is read-only; it reports findings, it does not fix anything.
   - If code-reviewer finds real correctness bugs or scope problems: send those findings back to a **new** code-writer call to fix, then re-review. Loop at most twice before surfacing remaining issues to the user instead of looping forever.
   - Cosmetic/style nitpicks that don't affect correctness: note them in your final summary rather than looping.

4. **code-tester** — Spawn it to verify the change actually works (compile the sketch and/or run relevant Python checks, per CLAUDE.md's build/test commands). If it finds a failure, send it back to code-writer to fix, then re-test. Same two-loop cap as step 3.

## Rules

- Each agent spawn other than a fork starts cold — give it a self-contained brief (the relevant plan/diff/findings), not "see above."
- Relay only what the next stage needs; don't dump full transcripts between stages.
- Never run `git commit` or `git push` as part of this pipeline unless the user explicitly asked for it in the request.
- If any stage is blocked on a decision only the user can make, stop and ask rather than guessing and continuing the pipeline.
- End with a short summary: what changed, what was reviewed/fixed, what was tested, and any open concerns — not a full replay of each agent's output.

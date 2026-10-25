# Task Coordination & Multi-Agent Tracking

The `docs/` directory coordinates work between AI assistants (Antigravity, Gemini, Claude, Codex, OpenCode, Cline) and human developers.

---

## Files Used

- `@docs/TASK_QUEUE.md` - The prioritized global task queue for all AI assistants.
- `@docs/TASK_LOG.md` - The historical execution log detailing specific steps, findings, and completed tasks.
- `@docs/TASK_LEFT_SUMMARY.md` - Executive summary of all remaining uncompleted tasks, grouped logically by subsystem.

---

## General Approach

- Always inspect `@docs/TASK_QUEUE.md` as well as `@docs/TASK_LOG.md` before starting new work.
- Consult `@docs/TASK_LEFT_SUMMARY.md` for an executive overview of open tasks and recommended priorities.
- Before adding a task, verify whether it already exists.
- **Multi-Conversation Isolation (Strict Invariant)**: Isolation applies not only across different agent families, but across **different conversations or sessions of the same agent identity**. Never touch, modify, or commit changes belonging to another conversation.

---

## Task Left Summary (`@docs/TASK_LEFT_SUMMARY.md`)

- Provides a clean, high-level executive summary of remaining open work.
- Groups uncompleted tasks logically by subsystem (e.g., Test Harness / KVM, Core DLLs, Wineserver Protocols, Conformance Tests, CI/CD).
- **CRITICAL RULE**: Completed tasks MUST be removed from `@docs/TASK_LEFT_SUMMARY.md`. They only remain as historical entries in `@docs/TASK_LOG.md` and marked as completed (`[x]`) in `@docs/TASK_QUEUE.md`. `@docs/TASK_LEFT_SUMMARY.md` must strictly and exclusively contain the list of things that still need to be completed—never keep completed tasks or mark them with `(COMPLETED)` in this file.

---

## Task Queue (`@docs/TASK_QUEUE.md`)

- Keeps track of work that must still be done. Each item should represent an atomic, well-defined task that can be completed in one sitting and verified with passing tests.
- Tasks use standard GitHub Flavored Markdown (GFM) task list items: `- [ ] ` for pending tasks, and `- [x] ` for completed tasks.
- **Claiming a Task (WIP Badge)**: When starting implementation, keep the `- [ ] ` checkbox and append an escaped agent WIP badge:
  ```markdown
  - [ ] Implement bcrypt BCryptGenerateSymmetricKey stub **`[ANTIGRAVITY-WIP 2026-10-14]`**
  ```
- **Completing a Task**: When finished and verified with passing tests, update the checkbox to `- [x] ` and append the completion badge:
  ```markdown
  - [x] Implement bcrypt BCryptGenerateSymmetricKey stub **`[ANTIGRAVITY-COMPLETED 2026-10-14: verified against Windows VM test harness with 18 assertions]`**
  ```
- **Markdown Specification Compliance & Square Bracket Escaping**:
  - All markdown files must strictly conform to CommonMark / GFM specifications.
  - In Markdown, unescaped square brackets `[text]` outside code blocks are parsed as link reference shortcuts. Unless defined, parsers and linters issue errors (`No link definition found`).
  - Therefore, agents **MUST wrap bracketed badges, tags, status markers, and array indices in inline code backticks** (`` **`[AGENT-WIP]`** ``, `` **`[AGENT-COMPLETED]`** ``, `` `array[0]` ``) and never use backslash-escaped brackets (`\` + `[` triggers KaTeX display-math mode in the Antigravity preview). Unescaped brackets are permitted only for standard links (`[text](url)`), defined reference links, and initial GFM task checkboxes (`- [ ] ` / `- [x] `).

---

## Task Log (`@docs/TASK_LOG.md`)

- Detailed step-by-step log of actions, findings, test outputs, and completion summaries.
- Entries MUST use escaped square brackets:
  ```markdown
  - **`[ANTIGRAVITY-WIP 2026-10-14]`** - Synthesizing differential test for BCryptGenerateSymmetricKey with invalid buffer pointers
  - **`[ANTIGRAVITY-COMPLETED 2026-10-14]`** - BCryptGenerateSymmetricKey returns STATUS_INVALID_PARAMETER under Windows 11; Wine implementation updated
  ```
- Always document:
  1. What was attempted.
  2. What passed or failed (with exact error codes/logs).
  3. Next steps required for full completion.

---

## Example Format

```markdown
# TASK_QUEUE.md

## High Priority Tasks

- [ ] Add WinRM execution bridge for Windows VM **`[GEMINI-WIP 2026-10-14]`**
- [x] Create differential test harness schema **`[CLAUDE-COMPLETED 2026-10-14: defined JSON output structure]`**

# TASK_LOG.md

- **`[GEMINI-WIP 2026-10-14]`** - Configuring WinRM HTTPS listener inside headless Windows 11 VM
- **`[CLAUDE-COMPLETED 2026-10-14]`** - Created JSON schema for comparing Wine vs Windows test outputs
```

---
trigger: model_decision
description: Use this whenever staging changes, committing code, preparing commit messages, or when marcommit is triggered.
---

# Commit Guidelines & The Iron Law of Marcommit

The authoritative rules for staging changes, committing code, using the `marcommit` workflow, and avoiding unauthorized commits are documented in `docs/skills/COMMIT_GUIDELINES.md`.

- **THE IRON LAW**: `NEVER AUTOMATICALLY COMMIT CODE WITHOUT EXPLICIT USER INSTRUCTION`.
- Leave modified files in the working tree for user review.
- When explicitly requested to commit ("marcommit"):
  1. **Strict Conversation & Task Scope**: Stage ONLY files relevant to the single active task of the CURRENT conversation. Never sweep in or bundle uncommitted files from other conversations or agents.
  2. **No Broad Staging Commands (Mandatory)**: NEVER run `git add .`, `git add -A`, or `git add -u`. Explicitly specify each individual file path (`git add <file1> <file2>`). Audit staged files with `git diff --cached --name-only` before committing.
  3. **Single Task per Commit**: Each commit must cover strictly ONE logical task. Never bundle multiple tasks together.
  4. **Task Documentation Alignment & Markdown Compliance**: In `docs/TASK_QUEUE.md`, `docs/TASK_LOG.md`, and `docs/TASK_LEFT_SUMMARY.md`, update and complete ONLY the single task assigned to the current conversation. All markdown entries must strictly fulfill Markdown specifications properly, especially wrapping bracketed badges in inline code backticks (`` **`[...]`** ``) on agent badges, tags, and array/range notations.
  5. Update `README.md` if user-visible surfaces changed.
  6. Reconcile branch to `main`.
  7. Include the mandatory `Co-authored-by:` trailer matching your agent identity (Antigravity, Gemini, Claude, Codex, OpenCode).

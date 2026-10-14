# Documentation Standards & Markdown Compliance

This document defines repository standards for markdown documentation, task tracking, and source code comments.

---

## 1. Markdown Specification Compliance (Strict CommonMark / GFM)

All documentation, task files, and pull request descriptions must strictly fulfill CommonMark and GitHub Flavored Markdown (GFM) specifications.

### The Square Bracket Rule (Mandatory)
In Markdown, text enclosed in unescaped square brackets (`[text]`) outside of fenced code blocks is parsed as a link or link reference shortcut. Unless defined with a matching link reference (`[text]: url`), Markdown linters and parsers flag this as an error (`No link definition found for '...'`).

**Rules for Agents:**
1. **Always escape square brackets** in agent WIP badges, completion tags, status markers, and array indices:
   - Correct: `**\[ANTIGRAVITY-WIP 2026-10-14\]**`
   - Correct: `**\[GEMINI-COMPLETED 2026-10-14: added test\]**`
   - Correct: `array\[index\]`
   - Incorrect: `**[ANTIGRAVITY-WIP 2026-10-14]**` (triggers broken link warning)
2. **Permitted unescaped brackets:**
   - Standard markdown links: `[Link text](https://example.com)` or `[Doc](docs/skills/ARCHITECTURE.md)`
   - Initial GFM task list items: `- [ ] ` and `- [x] `
   - Inline code spans: `` `[text]` ``

---

## 2. Task Tracking Files Synchronization

When completing a task, the following files must be kept in exact synchronization:
1. `docs/TASK_QUEUE.md`: Checkbox updated to `- [x] ` with escaped completion badge.
2. `docs/TASK_LOG.md`: Step documented with escaped completion badge and technical summary.
3. `docs/TASK_LEFT_SUMMARY.md`: **Strict removal of the completed task**. This file must exclusively contain remaining open work.

---

## 3. Source Code Comments

- Preserve all existing comments and license notices when modifying existing files.
- Focus code comments on **why** something was done (e.g. referencing a specific Microsoft documentation quirk or Windows behavioral oddity), not merely restating what the C syntax does.

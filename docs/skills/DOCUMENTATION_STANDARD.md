# Documentation Standards & Markdown Compliance

This document defines repository standards for markdown documentation, task tracking, and source code comments.

---

## 1. Markdown Specification & KaTeX Rendering Compliance (Strict CommonMark / GFM)

All documentation, task files, and pull request descriptions must strictly fulfill CommonMark, GitHub Flavored Markdown (GFM), and Antigravity KaTeX math preview specifications.

### The Square Bracket & KaTeX Compatibility Rule (Mandatory)
1. **CommonMark Link-Reference Hazard**: In Markdown, text enclosed in unescaped square brackets (`[text]`) outside of code spans is parsed as a link reference shortcut. Unless defined with a matching link reference (`[text]: url`), Markdown linters flag this as an error (`No link definition found for '...'`).
2. **KaTeX Display-Math Hazard (Backslash-Bracket)**: In the Antigravity IDE preview and Markdown viewers with KaTeX/LaTeX math enabled, a backslash-escaped bracket (`\` followed by `[` or `]`) outside code spans is interpreted as a **LaTeX display-math delimiter** (`$$ ... $$`), toggling math mode across consecutive lines and list items. This causes prose to render as an italic math equation with stripped spaces and throws a fatal `KaTeX parse error` whenever subsequent lines contain LaTeX control characters such as `#` (e.g. `PKCS#7`), `&` (e.g. `--security & scripts/lib/`), `%`, or `$`.

**Rules for Agents:**
1. **Always wrap bracketed agent badges, status markers, and array indices in inline code backticks (`` **`[...]`** `` or `` `[...]` ``) and NEVER use backslash-escaped brackets:**
   - Correct: `` **`[ANTIGRAVITY-WIP 2026-10-14]`** ``
   - Correct: `` **`[GEMINI-COMPLETED 2026-10-14: added PKCS#7 & ASan test]`** ``
   - Correct: `` `array[index]` ``
   - Incorrect: `**[ANTIGRAVITY-WIP 2026-10-14]**` (triggers CommonMark broken link reference warning outside backticks)
   - Incorrect: using backslash-escaped brackets on badges (triggers Antigravity preview `KaTeX parse error` across lines containing `#` and `&`)
2. **Permitted unescaped brackets outside backticks:**
   - Standard markdown links: `[Link text](https://example.com)` or `[Doc](docs/skills/ARCHITECTURE.md)`
   - Initial GFM task list items: `- [ ] ` and `- [x] `
   - Inline code spans: `` `[text]` `` or `` **`[text]`** ``

---

## 2. Task Tracking Files Synchronization

When completing a task, the following files must be kept in exact synchronization:
1. `docs/TASK_QUEUE.md`: Checkbox updated to `- [x] ` with inline-code completion badge (`` **`[AGENT-COMPLETED YYYY-MM-DD: <summary>]`** ``).
2. `docs/TASK_LOG.md`: Step documented with inline-code completion badge (`` **`[AGENT-COMPLETED YYYY-MM-DD]`** ``) and technical summary.
3. `docs/TASK_LEFT_SUMMARY.md`: **Strict removal of the completed task**. This file must exclusively contain remaining open work.

---

## 3. Source Code Comments

- Preserve all existing comments and license notices when modifying existing files.
- Focus code comments on **why** something was done (e.g. referencing a specific Microsoft documentation quirk or Windows behavioral oddity), not merely restating what the C syntax does.

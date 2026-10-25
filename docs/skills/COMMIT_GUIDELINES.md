# Commit Guidelines

## Conventional Commits

Use prefixes: `feat:`, `fix:`, `chore:`, `docs:`, `refactor:`, `test:`

## Best Practices

- Keep commits **buildable and focused**
- Describe intent, affected subsystems, and notable test flags
- Include reproduction commands or differential test cases
- For any AI agent sessions that are asked to document, stage, commit, and push the current session's work, explicitly follow the `marcommit` workflow. This workflow prevents unrelated agent changes from being staged accidentally.
- **AI-assisted commits must include a `Co-authored-by:` trailer** in the commit body (after a blank line separating it from the message body) identifying the agent that produced the work. Use the GitHub trailer format:
  ```
  Co-authored-by: Antigravity <antigravity@google.com>
  Co-authored-by: Gemini <gemini@google.com>
  Co-authored-by: Claude <claude@anthropic.com>
  Co-authored-by: Codex <codex@openai.com>
  ```
  Add one trailer per agent that meaningfully contributed to the commit.
  Human-only commits do not need this trailer.

## Marcommit Skill & Commit Ownership Policy (Mandatory)

- **NEVER automatically commit changes or small tweaks after completing tasks.**
- Always leave modified files in the working tree so the user can review and decide when to commit.
- Only run `git commit` / `marcommit` when the user explicitly requests or triggers a commit (e.g. `marcommit`, `commit these changes`, etc.).

The `marcommit` command is defined as a git alias in this repository:
```ini
[alias]
  marcommit = "!git commit -m \"$1\" --author=\"Marco Ermini <marco.ermini.dr@ieee.org>\" && git commit --amend --no-edit --trailer=\"Co-authored-by: [AGENT NAME] <[EMAIL_ADDRESS]>\""
```
Or via command line:
```bash
git config alias.marcommit '!git commit -m "$1" --author="Marco Ermini <marco.ermini.dr@ieee.org>" && git commit --amend --no-edit --trailer="Co-authored-by: [AGENT NAME] <[EMAIL_ADDRESS]>"'
```
*Note: If executing from a sandboxed environment where shell execution (`!`) is blocked, run the steps manually:*
1. `git commit -m "<message>" --author="Marco Ermini <marco.ermini.dr@ieee.org>"`
2. `git commit --amend --no-edit --trailer="Co-authored-by: [AGENT NAME] <[EMAIL_ADDRESS]>"`

When a user says `use marcommit`, interpret it as a strict commit hygiene workflow, not as a vague request to "just commit something quickly".

Required behavior:

1. Inspect `git status`, `git diff`, and `git diff --cached` before staging.
2. **Conversation & Task Scope Isolation**: Stage ONLY files and hunks that you personally authored for the specific task requested in the CURRENT conversation. Isolation applies strictly across different conversations/sessions as well as different agents. NEVER stage or commit changes made in another conversation or session, even if made by the same agent identity (e.g. another Antigravity or Gemini chat). Never adopt or bundle foreign uncommitted files from the working tree into your commit.
3. **No Broad Staging Commands (Mandatory)**: NEVER run `git add .`, `git add -A`, or `git add -u`. Always explicitly specify each individual file path: `git add <file1> <file2>`. Run `git diff --cached --name-only` to verify that strictly zero files outside your active task are staged. If any foreign file appears, unstage it immediately (`git restore --staged <file>`).
4. **Single Task per Commit**: Each commit must encompass strictly ONE logical task. Never bundle multiple tasks together into a single commit unless the user explicitly requested bundling those specific tasks.
5. **Mandatory Documentation & Task Updates**: Create all corresponding documentation for YOUR task. Complete and update `docs/TASK_QUEUE.md` and `docs/TASK_LOG.md`, and strictly remove completed tasks from `docs/TASK_LEFT_SUMMARY.md` (which must exclusively list remaining uncompleted tasks) ONLY for the single active task of the current conversation (see [TASK_COORDINATION.md](TASK_COORDINATION.md)). Any updates must be included directly in the commit. NEVER touch or complete entries for other tasks whose files happen to be in the working tree. **All markdown files (task files, documentation, README) must strictly fulfill Markdown specifications properly, especially wrapping bracketed badges in inline code backticks (`` **`[...]`** `` for agent badges, status indicators, and inline brackets) to prevent broken reference link warnings (`No link definition found`).**
6. **README Updates**: Describe tasks concisely in `README.md` if required (e.g. if user-visible surface, controls, CLI, or features changed).
7. Prefer committing on `main` only when the staged work is clearly non-overlapping with other in-flight edits.
8. If you have branched, reconcile the branch to `main` and delete the feature branch.
9. Always append a `Co-authored-by:` trailer to the commit body for every agent that contributed to the commit. Place it after a blank line separating the message body from the trailers. Use the exact format:
   ```
   Co-authored-by: Antigravity <antigravity@google.com>
   ```
   Replace the name and e-mail with the agent identity that actually performed the work. The agent must introspect its own identity — do not copy a `Co-authored-by:` line from a previous commit, from the `marcommit` alias default, or from any template:
   - If you are **Claude** (Anthropic): `Claude <claude@anthropic.com>`
   - If you are **Gemini** (Google): `Gemini <gemini@google.com>`
   - If you are **Antigravity** (Google): `Antigravity <antigravity@google.com>`
   - If you are **Codex** (OpenAI): `Codex <codex@openai.com>`
   - If you are **OpenCode** (Anomalyco) using a specific model: `OpenCode using <Model Name> <opencode@anomalyco.com>`
   - Other agents: use your canonical name and maintainer email.
   - If the agent is uncertain of its identity, it must ask the user before committing.
   If multiple agents contributed, add one trailer per agent.
10. In the final report, list the commit hash, message, push destination, and any intentionally excluded files.

## Never Commit

- Proprietary binaries, Windows OS ISOs, or unlicensed software
- VM disk images, qcow2 files, or memory dumps
- Build trees (`build/`, `builds/`, `build-*/`, `out/`, `bin/`)
- Logs, test execution traces, or debug captures unless explicitly required as regression fixtures
- Local configurations, IDE state, or credentials
- Secrets, certificates, tokens, or private keys

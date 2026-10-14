---
trigger: model_decision
description: Use this whenever picking up a task, logging progress, or updating task queue files.
---

# Task Coordination & Multi-Agent Hygiene

The authoritative task coordination guidelines and multi-agent workflow are documented in `docs/skills/TASK_COORDINATION.md`.

- **Multi-Agent & Multi-Conversation Collision Avoidance**: Always inspect `git status`, `docs/TASK_QUEUE.md`, and `docs/TASK_LOG.md` before starting work.
- **Strict Invariant**: NEVER touch files in the working tree or tasks marked `*-WIP` by another agent or originating from another conversation/session. Isolation applies across different conversations of the same agent identity as well.
- **Current Conversation Scope**: An agent must ONLY touch, stage, and commit changes for the single active task assigned in the current conversation. Never adopt or commit uncommitted files from other conversations.
- **WIP Declaration & Markdown Compliance**: Mark your assigned task as `\[<AGENT_NAME>-WIP\]` with a timestamp in `docs/TASK_QUEUE.md` and `docs/TASK_LOG.md` before writing any code. All markdown files written or updated (`TASK_QUEUE.md`, `TASK_LOG.md`, `TASK_LEFT_SUMMARY.md`) must strictly fulfill Markdown specifications properly, especially escaping square brackets (`\[...\]`) to avoid broken reference link warnings.

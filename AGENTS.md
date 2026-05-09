## Agent skills

### Build

Use `zig build` to compile the project. The `cmake`-based build is not configured in this workspace.

### Issue tracker

Issues live as GitHub Issues on `FatalDecomp/ROLLER`, accessed via the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Pull requests and merge gate

Agents may create pull requests after local verification and review, but must not merge PRs without explicit user approval in the current conversation. Green CI, passing local tests, and agent code review are not sufficient authorization to merge.

### Triage labels

All five canonical triage labels use their default names: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context repo. `CONTEXT.md` at the root when it exists. `docs/adr/` for architectural decisions. See `docs/agents/domain.md`.

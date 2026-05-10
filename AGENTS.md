## Agent skills

### Build

Use `zig build` to compile the project. The `cmake`-based build is not configured in this workspace.

### C function arguments

When a C function needs many arguments, do not blindly hide them in a catch-all struct. Group arguments into a named struct only when they form a real concept, such as a pose, camera, projection, viewport, color, or options block.

Prefer `const Struct *` for read-only grouped inputs. If many arguments are unrelated, treat that as a design smell: the function probably does too much and should be split or given clearer boundaries.

Use normal parameters for small, clear argument lists. Use structs when values are repeated across calls, are easy to misorder, or are likely to grow together over time.


### Issue tracker

Issues live as GitHub Issues on `FatalDecomp/ROLLER`, accessed via the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Pull requests and merge gate

Agents may create pull requests after local verification and review, but must not merge PRs without explicit user approval in the current conversation. Green CI, passing local tests, and agent code review are not sufficient authorization to merge.

### Triage labels

All five canonical triage labels use their default names: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context repo. `CONTEXT.md` at the root when it exists. `docs/adr/` for architectural decisions. See `docs/agents/domain.md`.

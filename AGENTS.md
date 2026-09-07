# AGENTS.md — Multi-agent tool compatibility declaration

This file declares how this skill (`rk3xx_chip_dev`) integrates with various
AI coding agents. It is the **single source of truth** for agent adapters.

## Supported agents

| Agent          | Loader file      | Invocation hint                          | Status |
|----------------|------------------|------------------------------------------|--------|
| CodeArts (华为) | `SKILL.md`       | auto-load via skill tool                 | ✅ primary |
| Cursor         | `.cursorrules`   | symlink to `SKILL.md` or copy relevant    | ✅ |
| Claude Code    | `CLAUDE.md`      | symlink to `SKILL.md`                    | ✅ |
| GitHub Copilot | `.github/copilot-instructions.md` | symlink `SKILL.md`       | ✅ |
| Aider          | `.aider.conf.yml`| reference `SKILL.md` in `read:` section  | ✅ |
| Continue       | `.continuerc.json`| add `SKILL.md` to `contextProviders`     | ✅ |
| Cline/Roo      | `.clinerules`    | symlink to `SKILL.md`                    | ✅ |

## How to adapt

This skill is agent-agnostic by design. The **only** agent-specific file is
`SKILL.md` (the entry point). All other files (`references/`, `knowledge/`,
`boards/`, `templates/`, `scripts/`, `docker/`) are plain Markdown/YAML/code
that any agent can read.

### For agents that auto-load `AGENTS.md` (Cursor, Claude Code, Cline, etc.)

Create a symlink or copy at the agent's expected path pointing to `SKILL.md`:

```bash
# Cursor
ln -s SKILL.md .cursorrules
# Claude Code
ln -s SKILL.md CLAUDE.md
# Cline
ln -s SKILL.md .clinerules
# GitHub Copilot
mkdir -p .github && ln -s ../SKILL.md .github/copilot-instructions.md
```

### For agents with config files (Aider, Continue)

**Aider** (`.aider.conf.yml`):
```yaml
read:
  - SKILL.md
  - boards/registry.yaml
  - references/*.md
```

**Continue** (`.continuerc.json`):
```json
{
  "contextProviders": [
    { "name": "file", "params": { "filePath": "SKILL.md" } }
  ]
}
```

## Tool dependencies

Scripts in `scripts/` and `docker/` use these tools. The skill works WITHOUT
them (agent can still read docs and generate code), but these enable execution:

| Tool           | Purpose                          | Required by              |
|----------------|----------------------------------|--------------------------|
| `python3`+`yaml`| `board_tool.py` board ops + diagnose | scripts/board_tool.py    |
| `sshpass`/`plink`| non-interactive SSH deploy      | scripts/deploy_run.sh    |
| Docker         | cross-compile images             | docker/build.sh          |
| `aarch64-linux-gnu-gcc` | cross compile C/C++      | scripts/build_templates.sh |
| `arm-linux-gnueabihf-gcc`| cross compile armhf      | scripts/build_templates.sh |
| `go`           | Go cgo build                     | templates/go-cgo-static  |
| `cross` (Rust) | Rust cross build                 | templates/rust-musl      |

## Platform notes

- **Windows**: use `docker/build.ps1` instead of `build.sh`. Use `plink`/`pscp`
  (PuTTY) instead of `sshpass`. PowerShell scripts use CRLF.
- **Linux/macOS**: use `*.sh` scripts. Install `sshpass` via apt/brew.
- **No Docker Desktop**: scripts in `scripts/` work standalone with a native
  cross-compiler toolchain. Docker is optional, only for hermetic builds.

## What this skill does NOT do

- Does NOT hardcode board IPs, passwords, or paths. All via `registry.yaml`
  or CLI args. See SKILL.md "Configuration" section.
- Does NOT assume a specific SoC. Capability matrix in `boards/*.md`.
- Does NOT bundle RK SDK (license-restricted). Mount via `--sdk` arg.
- Does NOT cover non-Rockchip platforms. For Allwinner/Amlogic, use a
  different skill.
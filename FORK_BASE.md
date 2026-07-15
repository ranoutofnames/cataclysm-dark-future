# Fork base

Cataclysm: Dark Future (working name) was created via GitHub's server-side repository importer from:

- **Upstream:** [`cataclysmbn/Cataclysm-BN`](https://github.com/cataclysmbn/Cataclysm-BN), branch `main`
- **Imported at commit:** `65859ffdba120e67608fad00d146ef15a5c7c8f2`
- **Subject:** `feat(mods/Arcana): audit weight and material requirements for several items (#9878)`
- **Date:** 2026-07-14

Because this was a real import (not a squashed snapshot), this repo shares full commit ancestry with upstream. Standard git porting works normally:

```
git remote add upstream https://github.com/cataclysmbn/Cataclysm-BN.git   # already set up
git fetch upstream
git merge upstream/main        # bulk sync — real 3-way merges
git cherry-pick <hash>         # pull one specific upstream commit
```

Files/subsystems we've deliberately rewritten (e.g. the Z-level system — see `Dark-Future-Dev-Docs/research/z-level-system.md`) will conflict on merge same as any other diverging fork; that's expected and hand-resolved same as with any git history, not specific to how this repo was created.

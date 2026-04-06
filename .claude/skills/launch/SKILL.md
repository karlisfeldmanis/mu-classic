---
name: launch
description: Launch the MU Online game client for testing. Use when the user wants to run or test the game.
disable-model-invocation: true
allowed-tools: Bash
---

Launch the game client from project root.

```bash
cd /Users/karlisfeldmanis/Desktop/mu_remaster && bash launch.sh
```

## Notes
- Always use `bash launch.sh` from project root (not running the binary directly)
- The launch script handles working directory and Data/ symlinks
- Client must be built first (`/build` skill)

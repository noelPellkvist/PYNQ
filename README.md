# Recreating projects

> Instructions for recreating projects based on platform and applications

---

## 📦 Pulling and recreating

```bash
git clone <repo>
cd <repo>

#This atleast is the path for me, edit if it's wrong
source /tools/Xilinx/2025.2/Vitis/settings64.sh

#This will open vitis, just click "ok" or "update" and then close it
vitis -w .

vitis -s scripts/create_platform.py
vitis -s scripts/create_app.py

```
## 🆕 New Project

When starting a new project, start with the platform and make sure you open "< workspace >/_ide/workspace_journal.py and save it somewhere else before closing vitis. Use this repos scripts/create_platform.py and scripts/create_app.py as a start, and add the commands that differs in the workspace_journal to add specific project settings after platform/app creating but before building.
"""Parse workflow YAML and syntax-check shell/Python blocks without running CI."""
from pathlib import Path
import ast
import re
import subprocess
import yaml

directory = Path(__file__).resolve().parents[1] / ".github/workflows"
release = yaml.load((directory / "release-validation.yml").read_text(), Loader=yaml.BaseLoader)
legacy = yaml.load((directory / "nightly-build.yml").read_text(), Loader=yaml.BaseLoader)
assert release["permissions"] == {"contents": "read"}
assert set(release["on"]) == {"push", "pull_request", "release", "workflow_dispatch"}
assert set(legacy["on"]) == {"workflow_dispatch"}
assert "pull_request" not in release["jobs"]["native-rebuild"]["if"]
assert "workflow_dispatch" in release["jobs"]["native-rebuild"]["if"]
assert "PS5_PAYLOAD_SDK_ARCHIVE_SHA256" in release["jobs"]["native-rebuild"]["if"]
blocks = python_blocks = 0
for workflow in (release, legacy):
    for job in workflow["jobs"].values():
        for step in job["steps"]:
            script = step.get("run")
            if script is None:
                continue
            subprocess.run(["bash", "-n"], input=script, text=True, check=True)
            blocks += 1
            for code in re.findall(r"python3 - <<'PY'\n(.*?)\nPY(?:\n|$)", script, re.S):
                ast.parse(code)
                python_blocks += 1
assert python_blocks >= 4
print(f"Workflow YAML, permissions/events, {blocks} Bash blocks and {python_blocks} Python blocks PASS; no remote action executed")

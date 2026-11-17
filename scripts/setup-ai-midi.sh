#!/usr/bin/env bash
# Explicit optional setup only: install an isolated CPU runtime and checksum-pinned model files.
set -euo pipefail
cd "$(dirname "$0")/.."
PROJECT_DIR="$PWD"
RUNTIME_DIR="${1:-$PROJECT_DIR/.deps/ai-midi}"
BOOTSTRAP_PYTHON="${COMPOSER_AI_BOOTSTRAP_PYTHON:-python3}"

"$BOOTSTRAP_PYTHON" - <<'PY'
import sys
if not (3, 9) <= sys.version_info[:2] <= (3, 12):
    raise SystemExit("Use Python 3.9–3.12; set COMPOSER_AI_BOOTSTRAP_PYTHON to its executable.")
PY
"$BOOTSTRAP_PYTHON" -m venv "$RUNTIME_DIR"
if [[ -x "$RUNTIME_DIR/bin/python" ]]; then
    RUNTIME_PYTHON="$RUNTIME_DIR/bin/python"
else
    RUNTIME_PYTHON="$RUNTIME_DIR/Scripts/python.exe"
fi
"$RUNTIME_PYTHON" -m pip install --disable-pip-version-check \
    'numpy==1.26.4' 'onnxruntime==1.19.2' 'Pillow==10.4.0' 'mido==1.3.3'
"$RUNTIME_PYTHON" - "$RUNTIME_DIR" "$PROJECT_DIR" <<'PY'
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
import sys
from urllib.request import Request, urlopen

runtime, project = (Path(value).resolve() for value in sys.argv[1:])
source_revision = "f504d5cb58f769ab0f2909c679238f6621034573"
model_revision = "0f8f265d4330f4e46527ac2313200254c5757f5f"
model_base = f"https://huggingface.co/skytnt/midi-model-tv2o-medium/resolve/{model_revision}"
source_base = f"https://raw.githubusercontent.com/SkyTNT/midi-model/{source_revision}"
files = [
    (model_base + "/onnx/model_base.onnx", runtime / "models/model_base.onnx",
     "1af68e16a0509936caeb70d322ab30619d784c7af72df019f3a59276c67c1bc4"),
    (model_base + "/onnx/model_token.onnx", runtime / "models/model_token.onnx",
     "cce9376abe8f5c8d51d1b110580cc8501d38679af55ebd5a332e757e62ab9911"),
    (model_base + "/config.json", runtime / "models/config.json", None),
    (source_base + "/midi_tokenizer.py", runtime / "vendor/midi_tokenizer.py", None),
    (source_base + "/LICENSE", runtime / "vendor/LICENSE-SkyTNT.txt", None),
]

def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()

def download(item):
    url, target, expected = item
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.is_file() and (digest(target) == expected if expected else target.stat().st_size > 0):
        print("Verified:", target.name, flush=True)
        return
    partial = target.with_name(target.name + ".part")
    try:
        print("Downloading:", target.name, flush=True)
        request = Request(url, headers={"User-Agent": "GeneticComposer-local-AI-setup/1.0"})
        with urlopen(request, timeout=120) as source, partial.open("wb") as output:
            shutil.copyfileobj(source, output, length=1024 * 1024)
        if expected and digest(partial) != expected:
            raise RuntimeError("Checksum mismatch: " + target.name)
        partial.replace(target)
    finally:
        partial.unlink(missing_ok=True)

with ThreadPoolExecutor(max_workers=2) as pool:
    list(pool.map(download, files))
shutil.copyfile(project / "scripts/ai-midi-bridge.py", runtime / "ai-midi-bridge.py")
(runtime / "runtime.json").write_text(json.dumps({
    "provider": "SkyTNT MIDI-Model tv2o-medium", "model_revision": model_revision,
    "source_revision": source_revision, "license": "Apache-2.0",
    "files": {str(path.relative_to(runtime)): digest(path) for _, path, _ in files}
}, indent=2) + "\n", encoding="utf8")
(runtime / "vendor/PROVENANCE.txt").write_text(
    "SkyTNT/midi-model\nhttps://github.com/SkyTNT/midi-model\n"
    "Source revision: " + source_revision + "\n"
    "midi_tokenizer.py is unmodified, under the upstream Apache-2.0 license.\n"
    "ONNX model: https://huggingface.co/skytnt/midi-model-tv2o-medium\n"
    "Model revision: " + model_revision + "\n", encoding="utf8")
print("Local AI installed:", runtime, flush=True)
PY
"$RUNTIME_PYTHON" "$PROJECT_DIR/scripts/ai-midi-bridge.py" \
    --check --output "$RUNTIME_DIR/check.mid" --model-dir "$RUNTIME_DIR/models" --vendor-dir "$RUNTIME_DIR/vendor"
printf 'Choose this interpreter in Genetic Composer: %s\n' "$RUNTIME_PYTHON"

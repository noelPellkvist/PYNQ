import vitis
import os
import shutil
import tempfile

APP_NAME = "app_component"
SRC_SUBDIR = os.path.join(APP_NAME, "src")

client = vitis.create_client()
client.set_workspace(".")

platform_xpfm = "$COMPONENT_LOCATION/../PYNQ_Platform/export/PYNQ_Platform/PYNQ_Platform.xpfm"

# ------------------------------------------------------------------
# 1. Backup existing src/ if it exists
# ------------------------------------------------------------------
tmp_dir = None
if os.path.isdir(SRC_SUBDIR):
    tmp_dir = tempfile.mkdtemp(prefix="vitis_app_src_")
    shutil.copytree(SRC_SUBDIR, os.path.join(tmp_dir, "src"))

# ------------------------------------------------------------------
# 2. Remove existing app_component (Vitis must recreate it)
# ------------------------------------------------------------------
if os.path.isdir(APP_NAME):
    shutil.rmtree(APP_NAME)

# ------------------------------------------------------------------
# 3. Create application (Vitis-owned)
# ------------------------------------------------------------------
client.create_app_component(
    name=APP_NAME,
    platform=platform_xpfm,
    domain="standalone_ps7_cortexa9_0",
)

# ------------------------------------------------------------------
# 4. Restore sources
# ------------------------------------------------------------------
if tmp_dir:
    restored_src = os.path.join(tmp_dir, "src")
    shutil.copytree(restored_src, SRC_SUBDIR, dirs_exist_ok=True)
    shutil.rmtree(tmp_dir)

# ------------------------------------------------------------------
# 5. Build application
# ------------------------------------------------------------------
client.get_component(APP_NAME).build()

vitis.dispose()

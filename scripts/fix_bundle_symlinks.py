#!/usr/bin/env python3
import os, sys, glob, shutil, subprocess

def fix_frameworks(app_path):
    frameworks_dir = os.path.join(app_path, 'Contents', 'Frameworks')
    if not os.path.isdir(frameworks_dir):
        return

    for fw in glob.glob(os.path.join(frameworks_dir, '*.framework')):
        fw_name = os.path.splitext(os.path.basename(fw))[0]
        versions_dir = os.path.join(fw, 'Versions')
        if not os.path.isdir(versions_dir):
            continue

        ver_a = os.path.join(versions_dir, 'A')
        if not os.path.isdir(ver_a):
            continue

        ver_current = os.path.join(versions_dir, 'Current')
        if os.path.islink(ver_current):
            os.remove(ver_current)
        elif os.path.isdir(ver_current):
            shutil.rmtree(ver_current)
        os.symlink('A', ver_current)

        root_bin = os.path.join(fw, fw_name)
        if os.path.exists(root_bin) or os.path.islink(root_bin):
            if os.path.islink(root_bin):
                os.remove(root_bin)
            else:
                os.remove(root_bin)
        os.symlink(os.path.join('Versions', 'Current', fw_name), root_bin)

        root_res = os.path.join(fw, 'Resources')
        if os.path.exists(root_res) or os.path.islink(root_res):
            if os.path.islink(root_res):
                os.remove(root_res)
            else:
                shutil.rmtree(root_res)
        if os.path.exists(os.path.join(ver_a, 'Resources')):
            os.symlink(os.path.join('Versions', 'Current', 'Resources'), root_res)

        root_headers = os.path.join(fw, 'Headers')
        if os.path.exists(root_headers) or os.path.islink(root_headers):
            if os.path.islink(root_headers):
                os.remove(root_headers)
            else:
                shutil.rmtree(root_headers)

    # 递归签名整个 app
    subprocess.run(['codesign', '--force', '--deep', '--sign', '-', app_path], check=False)
    print(f"[fix_bundle_symlinks] 成功规范化并完成 Ad-Hoc 签名: {app_path}")

if __name__ == '__main__':
    if len(sys.argv) > 1:
        fix_frameworks(sys.argv[1])
    else:
        print("Usage: fix_bundle_symlinks.py <path_to_app>")

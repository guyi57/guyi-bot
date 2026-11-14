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

    # 移除 Contents/MacOS 下非二进制的脚本目录
    bad_scripts = os.path.join(app_path, 'Contents', 'MacOS', 'scripts')
    if os.path.exists(bad_scripts):
        shutil.rmtree(bad_scripts)

    # 检测本机是否存在合法开发者证书 (Developer ID)，若存在则签名以获得持久稳定的 TCC 录屏/辅助功能权限
    identity = '-'
    try:
        out = subprocess.check_output(['security', 'find-identity', '-p', 'codesigning', '-v'], text=True)
        if "Developer ID Application: Beijing Knownsec Information Technology Co., Ltd (BTG378TG6Z)" in out:
            identity = "Developer ID Application: Beijing Knownsec Information Technology Co., Ltd (BTG378TG6Z)"
    except Exception:
        pass

    # 依次自内向外完成签名，避免 macOS TCC 权限因 cdhash 变化被系统重置
    sign_args = ['--force']
    if identity != '-':
        sign_args.extend(['--timestamp=none', '--sign', identity])
    else:
        sign_args.extend(['--sign', '-'])

    for pattern in [
        os.path.join(app_path, 'Contents', 'PlugIns', '*', '*.dylib'),
        os.path.join(app_path, 'Contents', 'Frameworks', '*.dylib'),
        os.path.join(app_path, 'Contents', 'Frameworks', '*.framework'),
        os.path.join(app_path, 'Contents', 'MacOS', '*.dylib'),
        os.path.join(app_path, 'Contents', 'MacOS', 'guyi-bot'),
    ]:
        for item in glob.glob(pattern):
            subprocess.run(['codesign'] + sign_args + [item], check=False)

    app_sign_cmd = ['codesign'] + sign_args + ['--identifier', 'com.guyi.guyibot']
    if identity == '-':
        dr = 'designated => identifier "com.guyi.guyibot"'
        app_sign_cmd.append('-r=' + dr)
    app_sign_cmd.append(app_path)
    subprocess.run(app_sign_cmd, check=False)
    print(f"[fix_bundle_symlinks] 成功规范化并完成稳定签名 ({'Developer ID' if identity != '-' else 'Ad-Hoc'}): {app_path}")

if __name__ == '__main__':
    if len(sys.argv) > 1:
        fix_frameworks(sys.argv[1])
    else:
        print("Usage: fix_bundle_symlinks.py <path_to_app>")

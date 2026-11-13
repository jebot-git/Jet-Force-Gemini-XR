# Sourced by launchers. The FLTK terminal's BDF-only FreeType cannot load TTF.
# Keep other user library search paths intact.
if [ -n "${LD_LIBRARY_PATH:-}" ]; then
    IFS=: read -r -a jfg_library_dirs <<< "$LD_LIBRARY_PATH"
    jfg_library_path=""
    for jfg_library_dir in "${jfg_library_dirs[@]}"; do
        case "$jfg_library_dir" in
            /opt/fltk-vr/bdf-runtime/lib) continue ;;
        esac
        jfg_library_path="${jfg_library_path:+$jfg_library_path:}$jfg_library_dir"
    done
    export LD_LIBRARY_PATH="$jfg_library_path"
    unset jfg_library_dirs jfg_library_dir jfg_library_path
fi
jfg_runtime_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/.runtime/usr/lib/$(uname -m)-linux-gnu"
if [ -f "$jfg_runtime_dir/libfreetype.so.6" ]; then
    export LD_LIBRARY_PATH="$jfg_runtime_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
unset jfg_runtime_dir

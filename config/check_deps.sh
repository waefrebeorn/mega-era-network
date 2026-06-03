#!/bin/bash
# F21: Check dependency versions against recorded baseline
cd "$(dirname "$0")"
echo "═══ F21: Dependency Version Check ═══"

check() {
    local pkg="$1" current="$2" recorded="$3"
    if [ "$current" != "$recorded" ]; then
        echo "CHANGED: $pkg $recorded → $current"
    else
        echo "OK: $pkg=$current"
    fi
}

while IFS='=' read -r pkg ver; do
    [[ "$pkg" =~ ^#.*$ || -z "$pkg" ]] && continue
    case "$pkg" in
        libcurl)   cur=$(dpkg -s libcurl4-openssl-dev 2>/dev/null | grep Version | awk '{print $2}' | cut -d- -f1) ;;
        libjansson) cur=$(dpkg -s libjansson-dev 2>/dev/null | grep Version | awk '{print $2}' | cut -d- -f1) ;;
        libsqlite3) cur=$(dpkg -s libsqlite3-dev 2>/dev/null | grep Version | awk '{print $2}' | cut -d- -f1) ;;
        gcc)       cur=$(gcc -dumpversion) ;;
        valgrind)  cur=$(valgrind --version 2>/dev/null | sed 's/valgrind-//') ;;
        *)         continue ;;
    esac
    check "$pkg" "$cur" "$ver"
done < deps.versions

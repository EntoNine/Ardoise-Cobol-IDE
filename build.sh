#!/bin/bash
# ─────────────────────────────────────────────────────────────
#  build.sh — Compile cwse_studio et génère le paquet .deb
#
#  Usage :
#    chmod +x build.sh
#    ./build.sh
#
#  Prérequis :
#    sudo apt install libgtk-4-dev libvte-2.91-gtk4-dev \
#                     libncurses-dev dpkg-dev
# ─────────────────────────────────────────────────────────────
set -e

PKG_NAME="ardoise-cobol-ide"
VERSION="1.0.0"
ARCH="amd64"
DEB_DIR="deb-package"
OUT_BIN="${DEB_DIR}/usr/bin/cwse_studio"

echo "━━━ 1/4 — Compilation ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
gcc cwse_ide.c cwse.c -o cwse_studio \
    $(pkg-config --cflags --libs gtk4 vte-2.91-gtk4) \
    -lncurses \
    -Wall -Wextra -O2

echo "  ✔  cwse_studio compilé"

echo "━━━ 2/4 — Mise en place du binaire ━━━━━━━━━━━━━━━━━━━"
mkdir -p "${DEB_DIR}/usr/bin"
cp cwse_studio "${OUT_BIN}"
chmod 755 "${OUT_BIN}"
echo "  ✔  binaire copié → ${OUT_BIN}"

echo "━━━ 3/4 — Permissions des scripts DEBIAN ━━━━━━━━━━━━━"
chmod 755 "${DEB_DIR}/DEBIAN/postinst"
chmod 755 "${DEB_DIR}/DEBIAN/postrm"
echo "  ✔  scripts DEBIAN configurés"

echo "━━━ 4/4 — Génération du .deb ━━━━━━━━━━━━━━━━━━━━━━━━━"
DEB_FILE="${PKG_NAME}_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "${DEB_DIR}" "${DEB_FILE}"
echo "  ✔  paquet généré : ${DEB_FILE}"

echo ""
echo "━━━ Installation ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  sudo dpkg -i ${DEB_FILE}"
echo "  # En cas de dépendances manquantes :"
echo "  sudo apt install -f"
echo ""
echo "━━━ Désinstallation ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  sudo dpkg -r ${PKG_NAME}"

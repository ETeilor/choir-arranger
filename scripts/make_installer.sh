#!/bin/bash
# Builds the macOS installer from the Release artefacts:
#   Choir Arranger.component -> /Library/Audio/Plug-Ins/Components
#   Choir Arranger.vst3      -> /Library/Audio/Plug-Ins/VST3
#   Choir Arranger.app       -> /Applications
# Result: dist/ChoirArranger-<ver>.pkg and dist/ChoirArranger-<ver>.dmg (the dmg holds the pkg and a readme).
set -euo pipefail
VER="${1:-0.4.0}"
ART="build/ChoirArranger_artefacts/Release"
OUT="dist"
WORK="$(mktemp -d)"
rm -rf "$OUT" && mkdir -p "$OUT"

stage() {   # stage <bundle> <subdir>
    mkdir -p "$WORK/$2"
    cp -R "$1" "$WORK/$2/"
    # Apple Silicon refuses unsigned code: ad-hoc signature (no Apple certificate needed)
    codesign --force --deep --sign - "$WORK/$2/$(basename "$1")"
}
stage "$ART/AU/Choir Arranger.component" au
stage "$ART/VST3/Choir Arranger.vst3" vst3
stage "$ART/Standalone/Choir Arranger.app" app

pkgbuild --root "$WORK/au"   --install-location "/Library/Audio/Plug-Ins/Components" \
         --identifier com.teilormusic.choirarranger.au   --version "$VER" "$WORK/au.pkg"
pkgbuild --root "$WORK/vst3" --install-location "/Library/Audio/Plug-Ins/VST3" \
         --identifier com.teilormusic.choirarranger.vst3 --version "$VER" "$WORK/vst3.pkg"
pkgbuild --root "$WORK/app"  --install-location "/Applications" \
         --identifier com.teilormusic.choirarranger.app  --version "$VER" "$WORK/app.pkg"

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Choir Arranger $VER</title>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="au"/><line choice="vst3"/><line choice="app"/>
    </choices-outline>
    <choice id="au" title="Audio Unit (Logic Pro)"><pkg-ref id="com.teilormusic.choirarranger.au"/></choice>
    <choice id="vst3" title="VST3"><pkg-ref id="com.teilormusic.choirarranger.vst3"/></choice>
    <choice id="app" title="Отдельное приложение"><pkg-ref id="com.teilormusic.choirarranger.app"/></choice>
    <pkg-ref id="com.teilormusic.choirarranger.au">au.pkg</pkg-ref>
    <pkg-ref id="com.teilormusic.choirarranger.vst3">vst3.pkg</pkg-ref>
    <pkg-ref id="com.teilormusic.choirarranger.app">app.pkg</pkg-ref>
</installer-gui-script>
EOF
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" "$OUT/ChoirArranger-$VER.pkg"

mkdir -p "$WORK/dmg"
cp "$OUT/ChoirArranger-$VER.pkg" "$WORK/dmg/Установить Choir Arranger.pkg"
cat > "$WORK/dmg/Прочитай меня.txt" <<'EOF'
Choir Arranger — установка

1. Дважды щёлкните «Установить Choir Arranger.pkg».
   Если macOS пишет, что разработчик не установлен: Системные настройки → Конфиденциальность
   и безопасность → внизу «Всё равно открыть», затем снова откройте установщик.
2. Установщик положит плагин в /Library/Audio/Plug-Ins (AU и VST3) и приложение в «Программы».
3. Перезапустите Logic Pro. Плагин: дорожка программного инструмента →
   AU Instruments → Teilormusic → Choir Arranger.
   Если Logic его не показал: Logic Pro → Настройки → Модули → Менеджер модулей,
   найдите Choir Arranger и нажмите «Сбросить и перепроверить».
EOF
hdiutil create -volname "Choir Arranger $VER" -srcfolder "$WORK/dmg" -ov -format UDZO "$OUT/ChoirArranger-$VER.dmg"
ls -la "$OUT"

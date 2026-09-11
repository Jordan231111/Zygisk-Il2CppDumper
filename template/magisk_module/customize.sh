#!/system/bin/sh
# Preserve explicit target/debug configuration during module upgrades.
for config in targets.txt verbose; do
  old="/data/adb/modules/zygisk_il2cppdumper/$config"
  if [ -f "$old" ]; then
    cp -f "$old" "$MODPATH/$config"
  fi
done
ui_print '- Enable Zygisk and reboot, then launch your authorized target app.'
ui_print '- targets.txt overrides the package selected at build time.'

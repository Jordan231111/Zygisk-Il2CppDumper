#!/system/bin/sh
# Preserve explicit target/debug configuration during module upgrades.
for config in targets.txt verbose unmount; do
  old="/data/adb/modules/zygisk_il2cppdumper/$config"
  if [ -f "$old" ]; then
    cp -f "$old" "$MODPATH/$config" || abort "Could not preserve $config"
  fi
done
ui_print '- Enable Zygisk and reboot, then launch your authorized target app.'
if [ -f "$MODPATH/targets.txt" ]; then
  ui_print '- Preserved targets.txt: it overrides the package selected in this build.'
  ui_print '- Remove targets.txt to use the new build target shown in module details.'
fi

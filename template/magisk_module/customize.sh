#!/system/bin/sh
# Preserve target/debug configuration; unmounting follows the newly installed ZIP.
for config in targets.txt verbose; do
  old="${NVBASE:-/data/adb}/modules/zygisk_il2cppdumper/$config"
  if [ -f "$old" ] && [ "$old" != "$MODPATH/$config" ]; then
    cp -f "$old" "$MODPATH/$config" || abort "Could not preserve $config"
  fi
done
case "$(grep_prop unmountModules "$MODPATH/module.prop")" in
  true)
    : > "$MODPATH/unmount" || abort 'Could not enable module unmounting'
    set_perm "$MODPATH/unmount" 0 0 0644 || abort 'Could not set unmount flag permissions'
    ui_print '- Module unmounting: ON for selected apps (may affect other modules).'
    ;;
  false)
    rm -f "$MODPATH/unmount" || abort 'Could not disable module unmounting'
    ui_print '- Module unmounting: OFF.'
    ;;
  *) abort 'Invalid unmountModules setting in module.prop' ;;
esac
ui_print '- Enable Zygisk and reboot, then launch your authorized target app.'
if [ -f "$MODPATH/targets.txt" ]; then
  ui_print '- Preserved targets.txt: it overrides the package selected in this build.'
  ui_print '- Remove targets.txt to use the new build target shown in module details.'
fi

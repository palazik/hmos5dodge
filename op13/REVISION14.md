# Revision 14: stock kernel module set

Not phone-tested when written (2026-11-17).

## Evidence from revision 12 (work/phone-attempt12/native-startup.log)

The SDE event log dumped at the first ESD failure shows the full display sequence:

| Time | Event |
|---|---|
| 13.400 s | Composer modeset: `dsi_display_set_mode`, panel init DCS, backlight 1433 (identical to stock Android's first enable). |
| 13.417 s | Commit with a real plane: 1440x3168 `AR24` buffer, `frame_done`, fences signalled. Scanout works. |
| 13.490 s | No further commits; encoder enters idle power collapse (`_sde_encoder_rc_idle`). |
| 13.505 s | `sde_cesta_resource_disable`: **mode2 entry failed, ret -16** (status 0x0 in all SCC registers). |
| 18.403 s | ESD worker (`dsi_display_check_status`) re-enables resources, reconfigures tearcheck. |
| 19.617 s | DSI command transfer failed (status 42), panel status read returns nothing, then `PANEL_DEAD conn 68 enc 67`. |

Stock Android boots (phone-20261109, phone-attempt3, attempt12 Android tail) never log the CESTA mode2 failure.

## Difference from stock Android

Stock vendor init (`init.qti.kernel.rc` early-init) runs `/vendor/bin/system_dlkm_modprobe.sh`, then
`/vendor/bin/vendor_modprobe.sh`. Together they load every system_dlkm module and the vendor_dlkm
`modules.load` list, minus blocklists, and tolerate individual failures.
The phone had 698 modules loaded under Android. The revision 12/13 bootstrap loaded 153 first-stage modules
plus a 130-row display/IPC closure. That left 466 stock modules unloaded, including power and regulator
providers (`qcom_lpm`, `qcom_stats`, `sys_pm_vx`, `qcom_amoled_regulator`, `qti_fixed_regulator`,
`pinctrl_spmi_gpio`), the power key (`qpnp_power_on`, `pm8941_pwrkey`), haptics, the GENI I2C/SPI buses and more.
Which specific module CESTA idle power collapse needs is not proven (no SDE source is available).

## Change

- `prepare_dlkm.expand_stock_load_order` builds the stock order: system_dlkm first, then vendor_dlkm `modules.load`.
  It applies `system_dlkm.modules.blocklist`, `zram` and `zsmalloc`, plus `modules.blocklist`.
  It also excludes the WLAN alternates `icnss2`, `qca_cld3_kiwi_v2` and `qca_cld3_peach`, because Android ends with `qca_cld3_peach_v2` only.
  Dependencies are ordered first. The original 130 display rows and `op13_hmos_ipc` stay required (fatal); the
  added rows are `optional` (failure is logged and loading continues).
  The result covers every module stock Android had loaded, except KernelSU.
- The bootstrap forwards `<module>.<param>=` kernel arguments to every module, as modprobe does
  (e.g. `oplus_bsp_tp_custom.dsi_display0`, `pci-msm-drv.pcie_sm_regs`). Values are not logged.
- The bootstrap log adds progress every 64 rows, each optional failure, and a summary line.
- No change to DSU partition binding, userdata policy, device filter, display HALs or graphics runtime.

## What to look for in the next phone log

- `stock modules: ... optional failed` summary, and which modules failed.
- Whether `mode2 entry failed` still appears after the first idle period, and whether ESD/PANEL_DEAD follows.
- If boot stalls in `stage=stock-modules`, the last `stock module progress` row bounds the culprit.

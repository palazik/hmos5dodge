# OP13 display registration

The revision-3 phone log passed DSU data initialization/mounting and the stock
module-load plan, then stopped because `/sys/class/drm/card0/dev` did not exist.
Loading `msm_drm.ko` successfully does not establish that its hardware probe
has completed or that its suppliers have registered.

## Panel boot arguments

The working phone supplies `msm_drm.dsi_display0=...` in its kernel command line,
and that value appears in the running display module's parameters. Android's
[`Modprobe::ParseKernelCmdlineOptions`](https://android.googlesource.com/platform/system/core/+/refs/heads/android16-release/libmodprobe/libmodprobe.cpp)
forwards module-specific boot arguments when loading a module. Our earlier
loader called `finit_module` with an empty argument string, dropping the panel
selector. The kernel parses the caller-supplied module argument string; loading
the `.ko` file alone does not reproduce Android's module-loader behavior.

Revision 4 forwards only the observed display selector keys, `dsi_display0`
and `dsi_display1`, from the phone's own boot arguments when loading msm_drm.
It strips the module-name prefix and preserves quoted values for the kernel
parser. It does not hardcode a panel or copy panel serials into the image.
Diagnostic logs record the number of forwarded selectors, not their values.
Unrelated boot arguments are not passed to the module. Regression cases cover
both selectors, quoting, unrelated/similar keys, malformed input and overflow.

## Hardware suppliers

A read-only capture of working Android found the display device at
`ae00000.qcom,mdss_mdp` bound to `msm_drm`, with a 1440x3168 DSI panel. Its
device-link graph, including the DSI controllers/PHYs and DP component, had
41 device nodes, 70 supplier edges and 11 bound module providers.

Seven providers were absent from the old symbol-only module plan:

- `clk_rpmh`
- `dispcc_sun`
- `gcc_sun`
- `pinctrl_sun`
- `qnoc_sun`
- `rpmh_regulator`
- `tcsrcc_sun`

Their symbol dependencies additionally select `icc_bcm_voter`, `icc_debug`,
`icc_rpmh` and `qnoc_qos`. These are unchanged modules from the same OP13
payload; no kernel or module binary patch is applied. The revised plan has
130 modules: 72 early copies from vendor_boot and 58 vendor_dlkm paths. All
785 checked module-to-module imports have matching CRCs. Running-kernel
exports and the new plan's actual DSU binding remain unverified.

All seven providers are also listed in stock vendor_boot's 153-entry early-load
list. They may already have been loaded before our bootstrap. The old log did
not inventory those bindings, so absence from our own load plan is not proof
of absence from the failed boot. Explicit supplier coverage, the bounded wait
and the retained kernel probe messages are tested changes for the next attempt;
they are not a confirmed diagnosis of the DRM failure.

`plan_display_suppliers.py` reads the already verified module inventory and
the private device-link capture, adds the observed suppliers before the DRM/GPU
consumer roots, rebuilds the symbol dependency closure and checks hashes/CRCs.
It reuses the existing extracted modules without making another image or
extraction-tree copy. It rejects captures without a bound msm_drm and providers
absent from the audited firmware.

```sh
python3 op13/plan_display_suppliers.py \
  --workspace /home/palaziks/hmosnext5_op13 \
  --audit /home/palaziks/hmosnext5_op13/out/module-audit/report.json \
  --capture /home/palaziks/hmosnext5_op13/work/phone-attempt3-supplier-graph.txt \
  --output /home/palaziks/hmosnext5_op13/out/module-audit/display-report.json
```

Use that new report as `--dlkm-audit` in the full builder or extender. The
capture belongs to the tested OP13/firmware; obtain matching observations if
the hardware or firmware changes. Keep raw device captures local.

Revision 4 also waits at most 10 seconds for a required sysfs device to
register. It accepts only actual major/minor values and retains the fatal
check on timeout. It does not fabricate DRM devices or ignore missing display
hardware. A stop now appends up to 64 KiB of kernel log tail to the bounded,
root-only bootstrap log, using a read-only kernel-log operation. This helps
retain probe errors when pstore is empty.

The missing suppliers are a supported explanation to test, not a recovered
kernel diagnosis of the failed DSU probe. The capture was from working Android
after that failed attempt. The next test must establish whether the complete
provider plan registers DRM and advances into native HarmonyOS init.

## Revision-4 phone result

The complete latest phone log passed required DRM/GPU/DMA-heap device discovery
and device-filter attachment, reaching the message before native init exec.
This establishes that the earlier missing-card0 stop was passed. It does not
prove modesetting, rendering, touch or successful native init/framework startup.
Revision 5 keeps this hardware plan and adds [persistent native diagnostics](NATIVE-STARTUP.md).

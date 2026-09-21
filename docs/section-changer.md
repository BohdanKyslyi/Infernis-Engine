# Item section changer

Any inventory item can act as a single-use section changer. Define the following
keys in the tool's item section (no new engine `class` is required):

```ini
[outfit_modernizer]
; Add the normal item fields (class, visual, icon, etc.) for your chosen item type.
base_section = novice_outfit
section_to_change = upgraded_novice_outfit
save_upgrades_from_old_section = true
```

In the actor inventory, drag the tool from the backpack onto an item with the
`base_section` section, either in the backpack or in an equipment slot. The
tool and the old item are consumed; the replacement appears in the backpack.
The old item's condition is carried over. A mismatch, an unconfigured target,
or a quest item leaves both items untouched.

`save_upgrades_from_old_section` defaults to `false`. When enabled, installed
upgrades are carried over only if both sections have the same `upgrade_scheme`
and each upgrade belongs to the replacement section's upgrade tree. Otherwise
the replacement starts with its own default upgrades. Other item-specific state
such as ammunition, attached addons, and consumable portions starts from the
replacement section's defaults.

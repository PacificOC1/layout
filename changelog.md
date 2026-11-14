# v0.3.2
- **Fix for v0.3.1, which kept almost all decoration** (a 42k object level gave a 38k copy). v0.3.1 asked a freshly made object what each ID is, but GD only marks objects as decoration while it loads a level. Copies ask the level's own objects again, as before v0.3.1, but not the ones with No Touch or Passable, so the saw fix stays.

# v0.3.1
- **Fix: saws (and other objects) missing from copies.** Copies decided what each object ID is from the level's first object with that ID. GD treats a No Touch hazard as decoration, so when a level's first saw of an ID had No Touch, every saw with that ID was left out, the ones that kill you too (seen on "cbf unl 3": all 91 ID 678 saws, 69 of them real hazards). Each ID is now classified from a fresh object, the way the game defines it. Applies to layout copies and low detail copies.

# v0.3.0
- **Low detail copy**: **Save low detail copy** in the LV popup saves "*name* LDM", which looks almost the same but has less to draw. Choose to remove the low value decoration or mark it High Detail, so GD's Low Detail Mode can leave it out.
- Low value decoration: already High Detail, never visible (Hide, or a channel that stays fully transparent), stacked duplicates on opaque non-blending channels, tiny ungrouped objects, and particle objects (halved by default). Shader triggers optionally. Decoration triggers use the position of is never touched.
- Also optional on everything: no glow, no orb/pad/portal particles, no fade/enter effects.
- The copy code now shares one level string reader, and CI runs the new tests.

# v0.2.0
- Layout copies play the same as the original: decoration and No Touch objects that a trigger uses the position of (rotate centre, move / teleport target, follow group, camera target...) or that are group parents now stay, hidden, instead of being dropped.
- Copies leave out more: unused triggers (move, spawn, toggle, rotate, follow, count... with nothing left in their target group), alpha and fade triggers, stacked duplicate blocks / spikes / slopes, and more visual triggers (ghost trail, show/hide player, BG / MG speed, edit middleground, area / enter tint, camera and BPM guides). Each has its own setting.
- Collision state blocks and toggle blocks are always kept.
- Layout copies of your own uploaded levels are allowed.
- Shader skipping only happens when it's safe, so it can't give a black screen.

# v0.1.0
- Initial release: layout view while playing, and layout copies.

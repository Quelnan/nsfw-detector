# NSFW Level Detector v1.3

Pattern-based NSFW detection. Looks for actual hiding techniques, not just trigger counts.

Detects:
- Off-screen art clusters (objects placed way above/below the level)
- Hidden color reveals (color triggers targeting off-screen object channels)
- Lag machines (spawn trigger loops, extreme trigger density in small areas)
- Camera jumps (camera triggers with extreme offsets pointing to hidden content)
- Toggle reveals (large groups of off-screen objects controlled by toggles)
- Alpha flashing (rapid opacity changes on channels with many objects)

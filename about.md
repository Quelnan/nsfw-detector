# NSFW Level Detector

A safety tool for streamers and content creators to check Geometry Dash levels for hidden NSFW content **before playing them on stream**.

## How it works

Press the **green scan button** on any level's info page. The mod analyzes the level data and shows a breakdown of suspicious patterns.

### What it detects

- **Skin Tone Analysis** — Checks if color channels (especially hidden ones) use skin-tone colors. This is the strongest signal for NSFW art.
- **Off-Screen Art** — Finds clusters of decoration objects placed far above or below the normal play area, where NSFW artists typically hide their work.
- **Hidden Coloring** — Detects the technique of leaving objects uncolored, then revealing them with stacked Color Triggers.
- **Camera Tricks** — Catches Static and Camera Offset triggers that jump the view to hidden areas containing art.
- **Lag Machines** — Identifies Spawn Trigger loops and extreme trigger density designed to freeze the game on NSFW content.
- **Toggle Reveals** — Finds Toggle triggers controlling large groups of off-screen decoration objects.
- **Alpha Flashing** — Detects rapid opacity changes on channels used by off-screen art.

### How to read the results

Each category shows a percentage:
- **0-15%** — Looks clean
- **15-40%** — Minor flags, likely safe
- **40-65%** — Suspicious, worth checking manually
- **65%+** — Likely hidden NSFW content

The **Skin Tone** category is the strongest indicator. If it flags above 50%, the level very likely contains NSFW art.

## Important

> **⚠️ This mod is a heuristic scanner, not a perfect detector.**
>
> - **False positives may occur.** Levels with complex decoration, desert themes, or sky art may trigger minor flags.
> - **False negatives may occur.** Some well-hidden NSFW content may not be detected.
> - **Do not use this as your only check.** Always verify suspicious levels manually before streaming or taking moderation action.
> - **Do not ban users based solely on this mod's results.** Use it as a first-pass filter to decide what needs manual review.

## For streamers

This mod was built specifically for you. The typical workflow is:

1. Someone requests a level
2. Go to the level's info page
3. Press the green scan button
4. If it flags high → check the level in the editor before playing on stream
5. If it looks clean → you're probably good, but use your judgment

## Feedback

Found a false positive or a missed NSFW level? Let the developer know so the detection can be improved.

This mod is in active development. Your feedback makes it better for everyone.

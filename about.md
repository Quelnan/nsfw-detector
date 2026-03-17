# NSFW Level Detector

A tool for **streamers** and content creators to check if a Geometry Dash level contains hidden NSFW content before playing it on stream.

## How it works

Adds a **Scan** button to the level info page. When pressed, it analyzes the level data for common NSFW-hiding techniques:

- **Art Detection** - Dense clusters of objects forming pixel art or vector art, especially in off-screen areas
- **Color Tricks** - Objects placed without color that get colored via Color Triggers to reveal hidden images
- **Lag Machines** - Trigger spam designed to freeze the game on NSFW content
- **Camera Tricks** - Static/Camera triggers that teleport the view to hidden areas
- **Toggle Reveals** - Groups of objects that start hidden and get toggled visible briefly
- **Alpha Flashing** - Brief opacity changes to flash inappropriate content

## Results

Each category shows a **percentage score** from 0-100%:
- 🟢 0-20% = Clean
- 🟡 20-50% = Suspicious  
- 🟠 50-75% = Likely hidden content
- 🔴 75-100% = Almost certainly hidden content

## Note

This is a heuristic detector - it can have false positives (flagging legitimate art levels) and false negatives. Always use your own judgment as well!

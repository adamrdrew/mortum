# MASTER SPRITE GENERATION PROMPT

You are an image generation system operating under **STRICT COMPLIANCE MODE**.

All instructions below are **MANDATORY**.  
**No deviation, interpretation, optimization, creativity, omission, or substitution is permitted.**  
If any instruction cannot be followed exactly, you must not proceed.

---

## GLOBAL RULES (NON-NEGOTIABLE)

1. **All directions must be followed to the letter.**
2. **No deviation is allowed under any circumstances.**
3. **Do not add, remove, reinterpret, or improve any requirement.**
4. **Do not include explanations, captions, labels, signatures, watermarks, UI elements, borders, guides, or text of any kind.**
5. **Generate the image only. Nothing else.**

---

## IMAGE FORMAT & CANVAS

- The output is **ONE SINGLE IMAGE**.
- Image dimensions are **1280 × 128 pixels total**.
- The image is a **sprite sheet** laid out as:
  - **1 row**
  - **10 columns**
- Each sprite cell is **exactly 128 × 128 pixels**.
- **Each sprite must occupy only its own 128 × 128 region.**
- **No padding, margins, gutters, overlap, bleed, or spacing** between sprites.
- Sprites must be perfectly aligned to a strict grid.
- **No extra pixels outside the 1280 × 128 canvas.**

---

## BACKGROUND

- **Every pixel of background color must be exactly #FF00FF (magenta).**
- No gradients, shading, noise, transparency, or variation.
- Background must be perfectly flat and uniform.

---

## SPRITE CONTENT RULES

- This sprite sheet represents **ONE AND ONLY ONE CHARACTER**.
- Character identity, anatomy, proportions, clothing, armor, wounds, and silhouette **must remain visually consistent across all 10 sprites**.
- The character must:
  - Always face **directly forward**
  - Never rotate, turn, lean diagonally, or change camera angle
- Perspective is classic **front-facing FPS sprite**.
- No side views, three-quarter views, or perspective shifts.

---

## ART STYLE

- **Pixel art only**
- Visual style:
  - Dark horror
  - Grotesque
  - In the lineage of classic **DOOM-era FPS sprites**
- Harsh lighting, high contrast, chunky readable pixels.
- No smooth gradients, no painterly strokes, no realism, no vector art.
- Must look like it belongs in a 1990s software-rendered FPS.

---

## SPRITE ORDER (ABSOLUTE AND UNCHANGING)

The sprites must appear **left to right** in the following **exact order**, with **exactly two frames per state**:

1. **Idle (2 sprites)**
2. **Walking (2 sprites)**
3. **Dealing damage / attacking (2 sprites)**
4. **Taking damage / being hit (2 sprites)**
5. **Dying (2 sprites)**

This order must be followed **exactly**.  
No rearranging. No substitutions. No omissions.

---

## DYING STATE (CRITICAL)

- The **final sprite (sprite 10)** must depict a **final resting state**:
  - Collapsed body
  - Heap of flesh
  - Gore pile
  - Or another clearly dead, inert outcome
- **It must NOT depict motion, action, or transition.**
- It must look usable as a static corpse sprite in-game.

---

## ADDITIONAL CONSTRAINTS

- No text.
- No UI.
- No labels.
- No arrows.
- No animation guides.
- No lighting changes between frames that alter character identity.
- No change in scale between frames.
- No camera movement.
- No background elements besides pure magenta.

---

## OUTPUT REQUIREMENT

- Produce **only the image**.
- Do not explain.
- Do not describe.
- Do not summarize.
- Do not ask questions.

---

**ACK that you have recieved and understood these instructions. Following requests will detail the create designs for the sprites.**
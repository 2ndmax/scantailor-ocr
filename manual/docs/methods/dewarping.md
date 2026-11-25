# Dewarping

A thick book doesn't lie flat on the scanner. Near the spine the page curves away, and
the lines of text bend and run together. A camera that isn't exactly above the page adds
a **trapezoid** distortion: the page is wider at one end than at the other.

**Dewarping** straightens both. It is in *Output > Dewarping*: tick **Dewarp** and
choose a **Method**.

## How it works

Dewarping builds a model of the page surface: a curve along the top of the text block,
a curve along the bottom, and straight lines between them. The page is then pulled flat
so that the curves become straight, parallel lines. The *Dewarping* tab of the page view
shows these curves.

## The methods

### Auto

Finds the lines of text and measures how they bend.

- If the lines are bent, it builds the curves from them.
- If the lines are straight, it doesn't bend the page, but still corrects a trapezoid
  distortion, like *Trapezoid*.
- It straightens lines that run together towards the spine, as on scans of thick books,
  where the page is flat and only the last part of each line bends.
- Edges of the text block are only taken from line ends that at least half of the lines
  share, so ragged text and verse don't pull an edge askew.
- A page with too few usable lines is left undewarped, and the panel says so.

### Trapezoid

Takes the lines of text as **straight** and corrects only the trapezoid distortion. Use
it for flat pages photographed at a slight angle. A page with fewer than 8 lines of text
stays as it is.

### Marginal

Takes the curves from the **top and bottom edges of the page** against a dark background,
not from the text. Useful for pages with little text, such as pictures, as long as the
paper edges are visible against a dark background.

### Manual

You place the curves yourself on the *Dewarping* tab:

- Click on a curve to add a control point.
- Drag a point to move it; hold ++ctrl++ or ++shift++ to drag along an axis.
- Press ++d++ to delete the point under the mouse.

Use it for pages where the automatic methods fail. *Auto* and *Trapezoid* pages switch to
*Manual* when their output resolution is changed, so they keep the curves they found.

## Settings

| Setting | Meaning |
|---|---|
| **Post deskew** | Straightens the page once more after dewarping. *Manual* and *Marginal* only. |
| **Depth perception** | How strongly the page is taken to curve away from the viewer. It mostly changes how far the text near the spine is stretched. Not used with *Trapezoid*. |
| **Even out letter widths** | See below. |

### Even out letter widths

Pulling a curved page flat stretches it, but not always by exactly the right amount.
Letters near the spine can come out too narrow, those further away too wide.

This option measures the width of the letter strokes across the dewarped page and
stretches or compresses the page where they came out too narrow or too wide. Pages with
too little text, or with differences below 3 %, are left alone. It roughly doubles the
time of the dewarping.

!!! tip
    Dewarping only helps pages that are actually curved or distorted. On flat scans from
    a flatbed scanner, leave it off: it costs time and can't improve anything.

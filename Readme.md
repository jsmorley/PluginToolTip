# ToolTip

`ToolTip.dll` is a C++ Rainmeter plugin for tooltips opened and closed with bangs from a meter or
the `[Rainmeter]` section.

The plugin draws customizable rectangle and balloon tooltips using Rainmeter's bundled
DirectWrite text formatter, including its inline formatting and PCRE pattern engine.

## Basic use

```ini
[TipExample]
Measure=Plugin
Plugin=ToolTip
DynamicVariables=1
Text=CPU usage: [MeasureCPU]%
FontFace=Segoe UI
FontSize=12
FontColor=245,245,250,255
SolidColor=25,35,52,255
Padding=12,10,12,10
StrokeSize=2
StrokeColor=70,170,235,255
ClipString=2
ClipStringW=320
AutoPopDelay=0

[MeterExample]
Meter=String
Text=Hover me
MouseOverAction=[!CommandMeasure TipExample "Show"]
MouseLeaveAction=[!CommandMeasure TipExample "Hide"]
```

To attach a tooltip to the skin background, put the same mouse actions in `[Rainmeter]`:

```ini
[Rainmeter]
Update=1000
MouseOverAction=[!CommandMeasure TipBackground "Show"]
MouseLeaveAction=[!CommandMeasure TipBackground "Hide"]
```

The tooltip measure holds its options. The meter or skin section only issues bangs; the plugin
does not take over mouse actions, install a mouse hook, or infer a target meter. Include `Hide`
in the matching leave action to cancel pending displays as well as close visible tooltips.
Avoid also setting Rainmeter's built-in `ToolTipText` on the same target, which can open a
second tooltip.

For beveled outlines, see [Configuring Bevels](#configuring-bevels).

## Commands and measure values

| [SP]20[/SP]Command[SP]20[/SP] | Result |
| --- | --- |
| `Show` | Opens at the mouse position after the configured delay. |
| `Show x y` | Opens using an anchor relative to the skin's upper-left corner, in pixels. Coordinates scale with Windows DPI and Rainmeter zoom; negative and fractional values are supported. The tail of a balloon tooltip attempts to stay at these coordinates when FollowMouse=0. |
| `Hide` or `Dismiss` | Immediately closes this instance and cancels a pending display. |
| `Toggle` or `Toggle x y` | Closes this instance if requested/open; otherwise behaves like Show. |
| `Update` | Resolves the current options/text and redraws this instance immediately if open. |

For example: `[!CommandMeasure TipExample "Show 400 300"]`.

The numeric measure value is `1` while visible and `0` while hidden or waiting for its delay.
The string value is the most recently resolved text, retained when hidden. Commands are
case-insensitive. Invalid commands/coordinates and invalid inline options are logged.

## Multiple instances

Use any number of differently named `Measure=Plugin` / `Plugin=ToolTip` measures in one skin.
Every instance owns its window, resolved text, layout, icon, timers, pending display, and visibility.
Instances share one Direct3D/Direct2D device to avoid multiplying driver
memory as different tooltips are shown. Hiding an instance releases its rendering
context, bitmap buffers, fonts/layouts and icon; those resources are recreated on
the next Show. Bitmap buffers are reused while an open tooltip's size and scale
stay unchanged. The shared device is released when the last measure is finalized.
Showing, hiding, updating, or removing one measure does not operate on the others. More than one
tooltip can be visible at once.

```ini
[TipFirst]
Measure=Plugin
Plugin=ToolTip
Text=First tooltip

[TipSecond]
Measure=Plugin
Plugin=ToolTip
Text=Second tooltip
Balloon=1
```

Target the appropriate measure name in each meter's actions. If a background tooltip is also
configured, explicitly hide it when entering a meter, as demonstrated in the example skin.

## Dynamic contents

While a tooltip is open, **every plugin measure update** resolves its text again, applies
inline patterns to the final text, redraws it, and adjusts its size and position. For normal
skins this happens on each skin update. Set `DynamicVariables=1` on each tooltip measure for
changing options and section-variable text such as `[MeasureCPU]`, `[MeasureTime]`, and `#Variables#`.
Use `#CRLF#` for line breaks.

`MeasureName`, `MeasureName2`, etc. can bind measures with `%1`, `%2`, etc. in `Text`. String-valued
measures keep their strings. Numeric measures support `NumOfDecimals`, `Scale`, `AutoScale`, and
`Percentual`. With a binding and no Text option, Text defaults to `%1`. With no binding it defaults
to empty. The resolved text also supports `Prefix`, `Postfix`, and `StringCase`.

`UpdateDivider` and Rainmeter's measure update controls still govern how often the plugin receives
Update. With `UpdateDivider=-1` or a paused skin there are no periodic content updates; call the
plugin's `Update` command when needed. Display delays, fade, timeout and dismissal use window
timers and do not require frequent skin updates. Redraws do not restart the timeout.

## Appearance

Color options accept `R,G,B`, `R,G,B,A`, six-digit `#RRGGBB`, or eight-digit `#RRGGBBAA` hex values.

Lengths below are pixels and are zoom / DPI aware; the plugin uses the skin's effective DPI/zoom scale when supported
by the Rainmeter API, including Windows display scaling and Rainmeter zoom exactly once.
The plugin exports HandleSkinSettingChange to declare DPI awareness to Rainmeter, preventing
the host from pre-scaling W, H and FontSize before the plugin scales them for rendering.
On older hosts without the effective-scale API, the plugin uses the skin window's Windows DPI.
A skin zoom or effective DPI-scale change dismisses open tooltips and cancels pending
InitialDelay displays, regardless of FollowMouse, AutoPopDelay or DismissOnClick.
A new Show command is required to open the tooltip again, using the new scale.
Ctrl+Alt-selecting the skin also dismisses open tooltips and cancels pending
InitialDelay displays. Show and Toggle cannot open a tooltip while that skin's
selection overlay exists, regardless of DismissOnClick or FollowMouse. After
deselecting the skin, a new Show command can open it normally. Selecting another
skin does not dismiss this skin's tooltip.
 `Show x y` coordinates are relative to the skin's upper-left
corner in these same scaled pixels. They follow the skin when it moves; after a scale
change, the next Show uses the new scale.

| [SP]20[/SP]Option[SP]20[/SP] | [SP]10[/SP]Default[SP]10[/SP] | Meaning |
| --- | --- | --- |
| `StrokeSize` | `1` | Outer outline width in pixels, including the balloon tail. Fractional values such as `0.5`, `1.25`, and `2.5` are supported; `0` disables the outline. Clamped to 0–32. Straight outer edges align to the physical pixel grid for crisp opaque strokes at whole-pixel widths; fractional widths, curves and the tail remain antialiased. |
| `StrokeColor` | `0,0,0,255` | Color of the outer outline; defaults to opaque black. |
| `W`, `H` | automatic | Overall tooltip body width/height in pixels, including padding and stroke, excluding the balloon tail. Zero or omitted means automatic sizing independently on each axis. |
| `Padding` | `8,6,8,6` | Left, top, right, bottom padding around the contents. |
| `Title` | empty | Optional heading using the selected font face and TitleFontSize in bold, with matching inline formatting applied. |
| `TitleFontSize` | `FontSize` | Base heading font size, independent of the body text. Matching inline Size settings override it. Updated while open, including the space reserved for the heading. |
| `TitleColor` | `FontColor` | Heading color, separate from the body text. Supports the same RGB/RGBA and hex formats as other colors, including a leading `#`. Unset or invalid values use the current FontColor. Updated while open. |
| `TitleX`, `TitleY` | automatic | Title origin from the body’s top-left corner, excluding the tail. Plain values are exact positions; r/R suffixes refer to the icon’s left/top or right/bottom edges. Without an icon, relative values use the body origin. Unset axes use automatic placement. |
| `TextX`, `TextY` | automatic | Body text layout origin in pixels. Plain values are coordinates from the rectangular body's top-left corner, excluding the tail. A suffix r/R refers to the title's left/top or right/bottom edges; without Title it refers to the icon, and without either it refers to the body origin. Unset/empty axes retain automatic placement. Inline formatting and text effects move with the text. |
| `Icon` | `None` | `Info` / `Information`, `Warning`, `Error`, `Question`, an `.ico` or `.svg` file path relative to the skin, or inline SVG markup. |
| `IconW`, `IconH` | `24`, `24` | Icon width and height in pixels (1–512). Set either dimension alone to calculate the other from the image's aspect ratio; set both to specify the icon area. If neither is set, the area is 24×24. SVGs are rasterized at the skin scale and retain their SVG aspect-ratio behavior. |
| `Balloon` | `0` | `1` adds rounded balloon corners and a tail. `0` draws a square rectangle. |
| `TailPosition` | `Center` | `Left` places the tail just left of the visible cursor, `Center` directly beneath it, and `Right` just right of it. Applies to mouse-anchored balloons in Auto/Below/Above positions. Case-insensitive; invalid values use Center. |
| `TailGap` | unset | Gap in physical screen pixels from the cursor's visible bottom to the popup's tail edge when below (top edge when above). Overrides OffsetY for vertical balloon placement. 0 touches the edge; values are rounded to whole pixels and clamped to 0–8192. Unset preserves OffsetY. Explicit Show x y uses the supplied point instead of cursor bounds. |
| `CornerRadius` | `6` | Balloon corner radius. The body uses native Direct2D rounded-rectangle geometry with an attached balloon tail. The balloon background and outline are supersampled for smoother curves; text and icons are drawn afterward at their original resolution to retain sharpness. Supersampling adapts to the tooltip size to limit temporary buffer memory, which is released when the tip closes. |
| `CloseButton` | `0` | `1` adds a clickable close button to the formatted heading row. |
| `CloseButtonX`, `CloseButtonY` | automatic | Top-left corner of the close button's 22 by 22 clickable area, measured from the rectangular body's top-left corner in pixels, excluding the tail. The X glyph is drawn inside that area. Requires CloseButton=1; unset/empty axes retain automatic placement. |
| `FontFace` | `Arial` | Font family. Installed fonts and fonts under `@Resources\Fonts` are supported. |
| `FontSize` | `10` | Font size in points, including fractional sizes. |
| `FontColor` | `0,0,0,255` | Text color, independent of the entire tooltip background. |
| `FontWeight` | `400` or `700` for bold StringStyle | Weight from 1 to 999. |
| `StringStyle` | `Normal` | Normal, Bold, Italic, BoldItalic. Explicit FontWeight takes precedence for weight. |
| `StringAlign` | `Left` | Left / Center / Right, with Top / Center / Bottom suffixes; also supports Justify. Alignment is within the text content box. |
| `StringCase` | `None` | None, Upper, Lower, Proper. |
| `StringEffect` | `None` | None, Shadow or Border, using Rainmeter's 1-unit text effect. |
| `FontEffectColor` | `0,0,0,255` | Text shadow/border color; separate from the tooltip stroke. |
| `AntiAlias` | `1` | Antialiased text; set 0 for aliased text. |
| `ClipString` | `2` | 0: no wrap/ellipsis; 1: wrap/trim within the available body area; 2: auto size up to optional maximums, wrapping/trimming as necessary. |
| `ClipStringW`, `ClipStringH` | unset | Maximum Text layout width/height with ClipString=2, also limited by the available space inside fixed W/H. |
| `TrailingSpaces` | `0` | 1 preserves leading/trailing spaces in the text returned by Rainmeter. |
| `Prefix`, `Postfix` | empty | Text before/after Text. |
| `NumOfDecimals` | `0` (1 with AutoScale or a decimal Scale) | Decimal precision for bound numeric measures, from 0 to 16. Percentual defaults to 0. |
| `Scale` | `1` | Divides bound numeric values; AutoScale takes precedence. |
| `AutoScale` | `0` | 0, 1 (1024), 1k, 2 (1000), 2k; suffixes k/M/G/T/P/E. |
| `Percentual` | `0` | 1 uses the bound measure's percentage of its Rainmeter range. |

The tooltip is a separate popup rather than a meter. Its W/H describe the rectangular body, excluding the tail, and
StringAlign describes alignment inside the available Text layout. General meter placement, transformations,
containers, meter actions, and meter groups are not tooltip layout options. Shadows can extend outside the calculated layout; provide sufficient padding/size.

## Configuring Bevels

Bevels shade the tooltip's outline. StrokeSize and StrokeColor set the underlying
stroke; the options below control its relief, profile and lighting. Dimensions are
pixels and are zoom / DPI aware.

### Quick start

For a raised beveled border, add these options to the tooltip measure:

```ini
StrokeSize=2.5
StrokeColor=110,110,110,255
StrokeBevel=Raised
StrokeBevelLightColor=220,220,220,255
StrokeBevelDarkColor=45,45,45,255
```

Use `StrokeBevel=Sunken` to reverse the bevel. The two bevel colors are optional;
omit them to derive the shading from `StrokeColor`. Fractional stroke sizes scale
with the skin and are antialiased; very thin outlines produce a subtler bevel.

For a softer rounded border, try:

```ini
StrokeSize=6.5
StrokeBevel=Raised
StrokeBevelProfile=Curved
StrokeBevelWidth=6.5
StrokeBevelRelief=0.8
StrokeBevelSmoothness=0.3
StrokeBevelLightAngle=225
```

`Linear` creates a flat sloping face. `Curved` softens the shading toward
the inner edge, `Concave` creates a recessed transition, and `Bulged` creates a
rounded rim with opposite slopes. `Ridge` and `Groove` add crests and valleys,
and repeated rings can form multiple rims. These profiles shade contour bands across the border,
including rounded balloon corners and tails. Wider strokes make the profiles more
visible. Raised/Sunken reverses each profile's lighting.

These controls are inspired by Microsoft's [BevelBitmapEffect](https://learn.microsoft.com/en-us/dotnet/api/system.windows.media.effects.bevelbitmapeffect).
They use Direct2D outline geometry with custom lighting and defaults;
they do not apply a bitmap bevel to the tooltip background, text, or icons.

### Bevel options

| [SP]20[/SP]Option[SP]20[/SP] | [SP]10[/SP]Default[SP]10[/SP] | Meaning |
| --- | --- | --- |
| `StrokeBevel` | `None` | `None` uses StrokeColor throughout. `Raised` lights the edge according to its local surface direction; `Sunken` reverses the relief. The default light direction highlights the top/left. Applies to rectangle and balloon outlines, including the tail. Values are case-insensitive. |
| `StrokeBevelLightColor` | derived from StrokeColor | Bevel highlight color. Defaults to StrokeColor blended 45% toward white, preserving its alpha. |
| `StrokeBevelDarkColor` | derived from StrokeColor | Bevel shadow color. Defaults to StrokeColor blended 45% toward black, preserving its alpha. |
| `StrokeBevelProfile` | `Linear` | `Linear`, `Curved`, `Concave`, `Bulged`, `Ridge`, or `Groove`. See the bevel profiles below. Case-insensitive; invalid values log a warning and use Linear. |
| `StrokeBevelWidth` | StrokeSize | Width of the shaded band within the outline, in pixels. Placement controls its position; Center is the default. Supports fractions and is clamped to 0–StrokeSize. The remainder uses StrokeColor; 0 gives a flat outline. |
| `StrokeBevelRelief` | `1` | Shading strength from 0 to 1; 0 gives a flat StrokeColor outline, 1 uses the full highlight/shadow colors. |
| `StrokeBevelSmoothness` | `0` | Softness from 0 (stronger directional contrast) to 1 (gentler directional transition). Also rounds the crest/valley of Ridge/Groove profiles. |
| `StrokeBevelLightAngle` | `225` | Light source direction in degrees clockwise: 0 right, 90 bottom, 180 left, 270 top. 225 lights the top/left. Angles wrap every 360 degrees. |
| `StrokeBevelPlacement` | `Center` | Position of a partial-width bevel band inside the existing stroke: Inner, Center, or Outer. Does not move the stroke or enlarge the tooltip. No visible effect when bevel width equals StrokeSize. |
| `StrokeBevelRings` | `1` | Number of repeated rims for Bulged, Ridge, or Groove profiles, from 1 to 8. Other profiles ignore it. More rings divide the same bevel width rather than increasing the stroke. |
| `StrokeBevelHighlight` | `0.25` | Extra specular highlight strength, 0–1, using StrokeBevelLightColor. Zero disables the glossy highlight without disabling ordinary bevel lighting. |
| `StrokeBevelGloss` | `24` | Specular highlight concentration, 1–128. Higher values give a tighter highlight; lower values give a broader sheen. Only matters when Highlight and Relief are nonzero. |
| `StrokeBevelShadow` | `1` | Directional shadow strength, 0–1, using StrokeBevelDarkColor. Zero removes darkening while preserving the lit face and specular highlight. |

### Profiles and rendering

A bevel is shading **within the stroke**, rather than a shadow outside the window
or a change to the background fill. Set StrokeSize wide enough to see it: 6–12
pixels is useful for comparison; very thin strokes cannot show a detailed profile.
StrokeBevel=None, StrokeBevelWidth=0, or StrokeBevelRelief=0 gives a flat StrokeColor
outline. StrokeSize=0 removes the outline entirely.

The renderer follows the local direction of the actual outline at each point,
including rounded corners and the balloon tail. It no longer applies a single
light/dark gradient across the tooltip's bounding rectangle. Rotating LightAngle
therefore changes which surfaces face the light.

Rectangle bevel faces meet along 45-degree miter seams across the full stroke
width, including its outer corners. Balloon corners follow their rounded outline.

The shaded cross-section gives each profile its shape:

| Profile | Appearance |
| --- | --- |
| Linear | A flat sloping bevel face. |
| Curved | A rounded shoulder, becoming flatter toward its inner edge. |
| Concave | A recessed curved transition. |
| Bulged | A smooth rounded rim with two differently lit faces. |
| Ridge | Two sloping faces meeting at a crest. Smoothness rounds the meeting point. |
| Groove | The inverse of Ridge: two faces meeting in a recessed valley. |

Raised/Sunken invert the profile's relief. Rings repeats Bulged/Ridge/Groove
across the same band, producing concentric rims or channels. For legible rings,
allow several pixels of bevel width per ring. Narrow strokes or many rings can
merge visually at the current scale. These are configurable profiles, not exact
copies of a particular application's bevel presets.

StrokeBevelWidth is limited to StrokeSize. When it is smaller, Placement chooses
where that shaded band sits **within the stroke**: Inner is nearest the contents,
Outer nearest the outside edge, Center between them. The unused stroke remains
StrokeColor. Placement does not put shading beyond the tooltip's outline, alter
W/H, change padding, or move the stroke centerline. Background images and text
are unaffected.

LightAngle uses degrees clockwise: 0 right, 90 bottom, 180 left, 270 top; 225
lights the top-left. Light elevation is fixed at 45 degrees with a frontal viewer.
Relief scales both diffuse and specular effects. Smoothness softens directional
contrast; Shadow independently reduces the dark face. Highlight and Gloss add and
shape the reflected highlight using LightColor, without replacing ordinary lighting.
For a matte edge, use Highlight=0. LightColor and DarkColor may also include alpha;
normal premultiplied-alpha compositing still applies.

For example, a rounded metallic rim:

```ini
StrokeSize=10
StrokeColor=175,150,70,255
StrokeBevel=Raised
StrokeBevelProfile=Bulged
StrokeBevelWidth=10
StrokeBevelRelief=0.85
StrokeBevelSmoothness=0.65
StrokeBevelLightAngle=225
StrokeBevelLightColor=255,244,181,255
StrokeBevelDarkColor=67,56,25,255
StrokeBevelHighlight=0.8
StrokeBevelGloss=40
StrokeBevelShadow=0.85
StrokeBevelRings=1
```

For two recessed rings, change Profile to Groove, Rings to 2, and use a sufficiently
wide stroke (for example 12 pixels). To retain a flat outer keyline, use a smaller
BevelWidth with Placement=Inner. All bevel options can change while a tip is open.

Outline geometry retains Direct2D antialiasing and the existing balloon surface
supersampling. Text/icons are drawn afterward at their normal resolution; bevel
lighting does not blur them. A per-measure lighting bitmap is reused until its
outline, scale, profile, lighting or colors change, and released when the tooltip
hides. Its storage is approximately capped at 4 MiB per open measure; for exceptionally
large tips only the lighting map is sampled at a lower resolution, while the outline
still renders at full resolution. Very wide bands at tight corners or the narrow
tail can meet or overlap naturally. The result is a shaded 2D outline, not ray-traced
metal, a cast shadow, or a bevel applied to the whole tooltip face.

## ToolTip Background

These options control the fill behind the tooltip's contents, including its padding
and balloon tail. Background images are drawn over the solid color or gradient;
transparent parts of the image let that underlying fill show through. The stroke
and all tooltip contents are drawn above the background.

### Solid colors and gradients

| [SP]20[/SP]Option[SP]20[/SP] | [SP]10[/SP]Default[SP]10[/SP] | Meaning |
| --- | --- | --- |
| `SolidColor` | `255,255,255,255` | Background for the **entire tooltip**, including title/icon area, padding, and balloon tail. Alpha is supported. |
| `SolidColor2` | unset | Optional second background color for a gradient over the whole tooltip. |
| `GradientAngle` | `0` | Background gradient angle in degrees; 0 goes left to right, 90 top to bottom. |
| `LinearGradient` | unset | Shape-style background definition: `Angle | Color ; Position | Color ; Position ...`. Takes precedence over SolidColor, SolidColor2 and GradientAngle when valid. Applies across the entire tooltip, including the heading and balloon tail. |

For a white-to-black background gradient, set `SolidColor=#ffffff`,
`SolidColor2=#000000`, and optionally `GradientAngle=90` for top to bottom.

### Background gradients with multiple stops

`LinearGradient` accepts an angle followed by one or more color stops, with up to 256 stops:

```ini
LinearGradient=180 | #FF0000 ; 0.0 | #00FF00 ; 0.5 | #0000FF ; 1.0
```

This creates a left-to-right red-to-green-to-blue background. RGB/RGBA colors and hex colors
with or without a leading `#` are supported, including transparency. Unlike the existing
`GradientAngle` option, this option follows the Shape meter's angle convention:

| Angle | Direction from first stop to last stop |
| --- | --- |
| 0 or 360 | Right to left |
| 90 | Bottom to top |
| 180 | Left to right |
| 270 | Top to bottom |

Angles wrap around 360 degrees. Stops are sorted by position, retaining their specified order
when positions are equal (use equal positions for abrupt transitions). Positions normally range
from 0.0 to 1.0; values outside that range are preserved and affect interpolation within the
visible range. A single stop produces a uniform color.

For a glossy glass-like surface, try a translucent highlight near the top:

```ini
LinearGradient=270 | #D6F3FFB0 ; 0 | #8ABBEA70 ; 0.28 | #263C60D0 ; 0.3 | #142542E8 ; 1
```

The definition is resolved again on every update while the tooltip is open, so it supports
variables with `DynamicVariables=1`. An empty or invalid definition falls back to the existing
SolidColor/SolidColor2/GradientAngle settings; invalid definitions also log a warning. The legacy
gradient's angle convention remains unchanged.

### Background images

| [SP]20[/SP]Option[SP]20[/SP] | [SP]10[/SP]Default[SP]10[/SP] | Meaning |
| --- | --- | --- |
| `BackgroundImage` | unset | Image file path, relative to the skin or absolute. Stretched over the entire tooltip, including the balloon tail, and clipped to its outline. Drawn over SolidColor / the background gradient and beneath the stroke and contents. Does not change tooltip sizing. If the extension is omitted, `.png` is assumed. |
| `ImageCrop` | unset | Source-image crop in pixels: `X,Y,W,H[,Origin]`. Positive W/H are required. Origin is `1` top left (default), `2` top right, `3` bottom right, `4` bottom left, or `5` center; X/Y are offsets from that origin. Regions outside the image are transparent. Applied before resizing. |
| `ImageTint` | `255,255,255,255` | Multiplies the image's red, green, blue and alpha channels by the specified color. White leaves it unchanged; an alpha below 255 makes it translucent. Supports RGB/RGBA and leading-`#` hex colors. Affects BackgroundImage only. |
| `ImageFlip` | `None` | `None`, `Horizontal`, `Vertical`, or `Both`, case-insensitive. Applied before ImageRotate. |
| `GreyScale` / `GrayScale` | `0` | Either spelling is accepted. `1` desaturates BackgroundImage before ImageTint is applied. Combine with ImageTint to recolor an image. If both are set, GreyScale takes precedence. Option names are case-insensitive. |
| `ImageRotate` | `0` | Clockwise rotation in degrees; negative values rotate counter-clockwise. The image rotates about its center, then is zoomed and center-cropped to keep the tooltip filled without blank rotation corners. This final fitting crop is separate from ImageCrop, which still selects the source region before transforms. Does not rotate the tooltip or its contents. |

Background image transforms follow Rainmeter's order: **crop, GreyScale, tint, flip,
rotate, then resize**. After rotation, an automatic centered zoom/crop keeps the
background covering the entire tooltip; only the image tilts. See Rainmeter's [general image options](https://docs.rainmeter.net/manual/meters/general-options/image-options/).
The image is decoded using Windows Imaging Component (for example PNG, JPEG, BMP,
GIF's first frame, TIFF and ICO; other formats depend on installed Windows codecs).
Transparent image areas show the existing SolidColor or background gradient. Text and
icons keep their existing rendering and are not tinted, flipped or rotated by these options.

```ini
[ToolTip]
Measure=Plugin
Plugin=ToolTip
Title=Background image
Text=The text and icon keep their normal colors.
BackgroundImage=#@#Images\Background.png
ImageCrop=-160,-90,320,180,5
GreyScale=1
ImageTint=80,160,255,220
ImageFlip=Horizontal
ImageRotate=15
SolidColor=20,25,35,255
FontColor=255,255,255,255
Balloon=1
W=240
H=90
```

The decoded/transformed image is cached for each open measure instance. Changes to
these options take effect on the next skin update or `Update` command; moving the
mouse does not reload the file. To reload changed file contents at the same path,
hide and show the tip or refresh the skin. Image resources are released when the tip
closes. Missing or invalid files log a warning once per image-option change/show and
fall back to the existing background. Invalid ImageCrop values log a warning and use
the full image. Decoded and transformed images are limited to 16,777,216 pixels each
and the graphics device's maximum bitmap dimensions.

## SVG icons

Use a file path (relative to the skin, or absolute):

```ini
Icon=#@#Icons\status.svg
IconW=24
IconH=24
```

Or put the complete SVG document on one INI line:

```ini
Icon=<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10" fill="rgb(80,210,160)"/><path d="M7 12l3 3 7-7" fill="none" stroke="white" stroke-width="2"/></svg>
IconW=24
IconH=24
```

Include a `viewBox` so the artwork scales to `IconW` and `IconH`; SVG's
`preserveAspectRatio` controls fitting within a rectangular viewport. The icon
appears directly left of `Title` when a title is defined, or directly left of
`Text` on the same row when no title is defined. Icons do not require a title.
A title without text does not reserve an empty text row. Transparency is preserved.
The title and icon are vertically centered in their shared row, accounting for
TitleFontSize and inline formatting. An explicit TitleY overrides automatic
title centering. Relative TextY values use the title's actual centered position.
When there is no title, Text is vertically centered on the icon using its
visible glyph bounds. An explicit TextY retains manual placement.
Automatic icon-row alignment accounts for the text pixels rendered at the current
scale. Text retains pixel snapping for sharp rendering; any remaining fractional
alignment correction is applied to the icon instead. Automatic row height uses
the visible content rather than adding the font's empty space below its glyphs.
SVG icons have transparent space above and below the artwork removed before
scaling and drawing. With only `IconH=26`, the visible artwork is 26 pixels
high; the missing width uses the vertically trimmed aspect ratio. Title, or
Text when there is no title, is centered against that same trimmed image.
Horizontal transparent margins are preserved. With both dimensions set, SVG's
`preserveAspectRatio` still controls fitting within that area, and any resulting
empty vertical margins are excluded from the drawn icon and tooltip layout.
Trimming is calculated when the source, dimensions or scale change; the SVG file
is not modified. A fully transparent SVG uses the full icon area as a fallback.
Explicit TitleY/TextY still override automatic text centering.
IconY positions the icon independently; setting TextY does not move the icon.
Automatic height includes positioned contents plus bottom padding and stroke clearance;
all element Y coordinates exclude the balloon tail.
These dimension options also resize standard and ICO icons.
For ICO files containing multiple embedded images, the plugin selects the smallest
image large enough for the requested icon dimensions after Rainmeter zoom and
Windows DPI scaling. If none is large enough, it uses the image with the best
size coverage. Equal-size candidates prefer higher color depth. The selected image
is drawn at the requested size; it does not change the icon's layout dimensions.
For example, IconW=42 at 275% zoom needs about 116 screen pixels, so a 125-pixel
image is preferred over a 42-pixel image when available. Selection is repeated
when requested icon dimensions change while open. A scale change dismisses the
tooltip; its next Show selects an image for the new scale. ICO files remain raster images: enlarging beyond the
largest available image can still soften or pixelate the artwork. Unreadable
frames are skipped; unreadable ICO files log a warning. ICO files are limited to
16 MiB. SVGs continue to rasterize at the effective skin scale.


Set only `IconW` or only `IconH` to preserve the image's aspect ratio and
calculate the missing dimension. For SVGs, the ratio uses the `viewBox`,
or intrinsic width and height when no viewBox is present, with vertical
transparent margins removed. If no usable
ratio is available, a square is assumed. Calculated dimensions are limited to
1–512 pixels. SVG root width/height attributes do not override the requested
icon size. Transparent margins to the left and right remain part of the image.

Rainmeter variables and measure references in `Icon`, `IconW` and `IconH` are
resolved on each skin update while the tooltip is open. For dynamic inline
colors, use `fill="rgb(#IconColor#)"` with `IconColor=80,210,160` in `[Variables]`.
Use RGB syntax instead of literal hexadecimal `#...` colors when Rainmeter
variable substitution could interpret the hash characters. A changed icon value,
dimensions or skin scale reloads the icon; editing an SVG file in place takes
effect on the next Show or skin refresh.

SVG rendering uses Windows Direct2D and requires Windows 10 Creators Update
(1703) or later. Direct2D supports
a [subset of SVG](https://learn.microsoft.com/en-us/windows/win32/direct2d/svg-support),
including paths, basic shapes, transforms and gradients. Use presentation
attributes such as `fill` and `stroke`; scripts, animation, text, CSS stylesheets,
filters and external image references are unsupported. Convert text to paths.
Invalid/unreadable SVGs log a warning and the tooltip continues without an icon.
SVG documents are limited to 16 MiB.

## Spacing Considerations

Positions and distances below are in **pixels**, aware of Rainmeter zoom and Windows DPI scaling.

### Default Behavior

With no manual size or positioning settings, the tooltip fits its contents:

| Element | Automatic behavior |
| --- | --- |
| Overall size | Width and height fit the positioned contents plus right/bottom padding and stroke clearance. The balloon tail adds 12 pixels outside the body. |
| Padding | 8 pixels left, 6 top, 8 right, 6 bottom. StrokeSize adds clearance to automatic content origins. |
| Icon with Title | Icon is beside Title, with 6 pixels between the icon's right edge and Title. Title is vertically centered against the icon's visible artwork. |
| Icon without Title | Icon is beside Text instead, with the same 6-pixel gap and vertical centering. |
| Title without Icon | Starts at the left/top content inset. |
| Text with Title | Starts at the left inset, 5 pixels below the title/icon row. |
| Text without Title or Icon | Starts at the left/top content inset. No empty title row is reserved. |
| Close button | When enabled, starts at the top-right. Its clickable area is 22 by 22 pixels; automatic layout reserves 24 pixels horizontally. The X glyph is inset inside that area. |

Automatic icon-row centering measures visible text pixels and preserves pixel
snapping to keep text sharp. SVG icons have transparent space above/below their
artwork removed before sizing and drawing; horizontal transparency remains part
of the image. Font layout boxes can still include whitespace beyond their visible
glyphs. Automatic Text measurement retains a small 1-pixel allowance. Text effects
can extend outside measured bounds, so allow room for them in padding.

### Manual Settings

**The origin for every element is the rectangular body's upper-left corner**,
excluding the balloon tail. Content positions therefore stay the same inside the
body when the tail flips above or below.

| Option | Default | What it changes |
| --- | --- | --- |
| `W`, `H` | automatic | Overall body width/height, including padding and stroke but excluding the tail. Set each independently. Omitted sizes that axis automatically. A fixed dimension stays fixed even if contents do not fit. |
| `Padding` | 8,6,8,6 | Left, top, right, bottom insets for automatic placement; right/bottom padding also contributes to automatic sizing. It is inside fixed W/H. |
| `IconX`, `IconY` | automatic | Exact icon position: pixels from the body's left edge and down from its top. IconX automatically horizontally places adjacent Title/Text; explicit TitleX/TextX overrides that relationship. IconY disables automatic icon/text vertical centering. |
| `TitleX`, `TitleY` | automatic | Exact Title origin, or relative to the icon using r/R. Without an icon, relative coordinates use the body origin. Explicit TitleY overrides automatic title centering. |
| `TextX`, `TextY` | automatic | Exact Text origin, or relative to Title using r/R. Without Title, the reference is Icon; without either, it is the body origin. Explicit TextY overrides automatic row spacing and vertical centering. |
| `IconGap` | 6 | Automatic horizontal gap between icon and adjacent Title/Text. Explicit TitleX/TextX bypasses it. |
| `TitleTextGap` | 5 | Automatic vertical gap between the title/icon row and Text. Explicit TextY, including a relative value, bypasses it. Has no effect without both Title and Text. |
| `CloseButtonX`, `CloseButtonY` | automatic | Exact top-left of the close button's 22 by 22 clickable area. The glyph and clickable area move together. Requires CloseButton=1. |

Set X and Y independently. Unset or empty axes keep automatic placement; explicit
zero means exactly zero. **Explicit coordinates do not add padding, stroke
clearance or automatic gaps.** Negative and fractional coordinates are allowed;
negative positions can be clipped and do not shift other contents into view.
Manual overlaps are allowed. Plain absolute coordinates accept formulas.
IconGap/TitleTextGap accept fractional values from 0 to 8192.

When setting IconY, set TitleY or TextY too if you want to arrange both manually.
Otherwise the text keeps its default origin without automatic icon centering.
Automatic TextY with a Title follows the title/icon row plus TitleTextGap; moving
the title or icon adjusts the row's bottom. An explicit TextY overrides that rule.

TitleX/Y and TextX/Y accept a number followed by `r` or `R`:

| Suffix | X reference edge | Y reference edge |
| --- | --- | --- |
| `r` | Left | Top |
| `R` | Right | Bottom |

The number is an offset from the referenced element's edge. For Title the
reference is Icon. For Text it is Title, falling back to Icon. When no reference
exists, both suffixes use the body's top-left corner. Text reference edges use
the font layout box rather than individual glyph edges. Relative offsets can be
negative or fractional; bare r/R means zero. Variables are resolved first;
relative offsets themselves are numeric, rather than formulas.

For example:

```ini
IconX=12
IconY=8
IconH=24

; Title starts 6 pixels right of the icon, at its top.
TitleX=6R
TitleY=0r

; Text starts at the title's left edge, 5 pixels below its bottom.
TextX=0r
TextY=5R
```

With no Icon or Title, TextX=10, TextX=10r and TextX=10R all mean 10 pixels from
the body's left edge. TextY=10, TextY=10r and TextY=10R all mean 10 pixels below
its top. An omitted TextX/Y instead uses automatic padding.

A fixed W provides the width available for text wrapping after its X position,
right padding, stroke clearance and an automatically placed close button. H
limits the available vertical area. ClipString and ClipStringW/H still control
text wrapping/clipping within that space. Moving text can change its available
width/height. Content outside a fixed body is clipped; it does not enlarge the
tooltip. With automatic W/H, moving Title, Text, Icon or the close button right/down
can enlarge the body to include its bounds, padding and stroke clearance.

For a fully specified layout:

```ini
W=280
H=100
Padding=0,0,0,0
IconH=24
IconX=12
IconY=12
TitleX=46
TitleY=12
TextX=12
TextY=44
CloseButton=1
CloseButtonX=246
CloseButtonY=8
```

Define Icon, Title and Text separately. Omit W/H for automatic size. Omit element
positions for automatic layout, optionally changing IconGap and TitleTextGap.
All settings and relative reference bounds update while the tooltip is open.

Even with W/H and every element position explicitly set, these behaviors remain:

* The balloon tail has a fixed 12-pixel height outside the body, scaled with zoom/DPI.
* Font metrics can leave whitespace above/below visible Title/Text glyphs. Exact
  text coordinates position the layout origin, not necessarily the first painted pixel.
* Setting only IconW or IconH calculates the missing dimension from the image's
  aspect ratio. SVG vertical transparent margins are trimmed before sizing/drawing;
  horizontal transparent margins remain.

Bottom padding is controllable through the fourth Padding value. With automatic H,
it is added below the lowest content bounds, together with stroke clearance. With
fixed H, padding does not enlarge the body: the visible space below Title/Text
comes from the chosen height, element positions and font metrics. Bottom padding
reduces the available Text layout height inside that fixed body. There is no extra
unconfigurable bottom-padding setting.

Any dimensions or positions left unset still use the default sizing, placement,
gaps and icon/text centering described above.


These rules change older W/H settings, which sized only the Text area, and older
absolute TextY/CloseButtonY settings, which included a top tail. Existing settings
may need adjustment; all values now follow the same body-based rules above.

## Inline string formatting

Use consecutive `InlineSetting`, `InlineSetting2`, etc., with matching `InlinePattern` options.
An omitted pattern defaults to `.*`. PCRE matching, lookarounds, Unicode, and capture groups are
supported. Captures receive the formatting when the pattern contains captures; otherwise the
whole match receives it. Each pattern is matched independently against `Title` and `Text`;
a rule can format either or both, depending on which values match. Text patterns apply after
bound values, prefix/postfix and StringCase. Title patterns use the current resolved `Title`.
Both are refreshed on each skin update while the tooltip is open. Inline font sizes and other
layout settings also affect the space reserved for the heading.

`TitleColor` remains the heading's base color; matching inline `Color` or `GradientColor`
settings override it for their matched ranges. For example:

```ini
Title=System status
Text=CPU: [MeasureCPU]%
TitleColor=#FFFFFF
InlineSetting=GradientColor | 180 | #FF6699 ; 0 | #66CCFF ; 1
InlinePattern=^System status$
InlineSetting2=Color | #66FF99
InlinePattern2=CPU: (\d+)%
```

The first rule gives the title a gradient, and the second colors the numeric CPU value in
the body. A pattern such as `.*` applies to both values; patterns cannot span across them.

### Current Title and Text references

`[$Title]` and `[$Text]` insert this measure instance's current title and text
into string options. Names are case-insensitive. `[$Text]` uses the resolved
text after measure bindings, numeric formatting, prefix/postfix and StringCase.
References are refreshed when shown and on each skin update while open.

In `InlinePattern` and its numbered variants, these variables match their values
**literally**. Parentheses, brackets, backslashes and other regex punctuation
in a title or text do not need escaping.

```ini
Title=Battery (68%)
Text=Logitech [MeasureLogitechName]
InlineSetting=GradientColor | 180 | #FF6699 ; 0 | #66CCFF ; 1
InlinePattern=[$Title]
InlineSetting2=Color | #66FF99
InlinePattern2=[$Text]
DynamicVariables=1
```

These rules format the complete current Title and Text respectively. Patterns
still run independently against both values; identical values match both.
An empty referenced value matches nothing, rather than formatting everything.
Do not add regex escaping or `\Q...\E` around the references yourself.

In other string options, values are inserted as plain text. References are
local to the plugin measure, not skin-wide Rainmeter variables or numeric
formula variables. Expansion makes one pass over each option; inserted values
are not recursively expanded. Title/Text definitions can reference the other
value from the same update's source snapshot without creating a recursion loop.

Supported settings follow the Rainmeter String meter syntax:

| [SP]20[/SP]Setting[SP]20[/SP] | Example |
| --- | --- |
| Face, Size, Color, Weight | `Face | Segoe UI`, `Size | 14.5`, `Color | #FF8844FF`, `Weight | 700` |
| Case | `Case | Upper` (also Lower, Proper, Sentence) |
| CharacterSpacing | `CharacterSpacing | 2 | 2 | *` (leading, trailing, minimum advance; * leaves a value unchanged) |
| Italic, Oblique, Underline, Strikethrough | `Underline` |
| Stretch | `Stretch | 7` (1–9) |
| Typography | `Typography | smcp` or `Typography | ss01 | 1` |
| GradientColor / GradientColor1 | `GradientColor | 180 | 255,0,0,255 ; 0 | 0,255,0,255 ; 1` |
| Shadow | `Shadow | 2 | 2 | 3.5 | 0,0,0,180` |
| None | `None` disables that setting without ending the numbered sequence. |

Inline gradients apply separately on each line; GradientColor1 selects the alternative gamma.
Font features and stretch depend on what the font supports. PCRE work is limited per pattern
to prevent an expensive expression from indefinitely blocking the skin. Invalid patterns are
logged and skipped. Up to 256 bindings and inline settings are supported per instance.

## Behavior and positioning

| [SP]20[/SP]Option[SP]20[/SP] | Default | Meaning |
| --- | --- | --- |
| `Enabled` | `1` | 0 prevents Show and hides an open instance on its next update. |
| `InitialDelay` | `0` | First display delay in milliseconds; default opens immediately. |
| `ReshowDelay` | `100` | Delay when reopened within one second of its last hide. |
| `AutoPopDelay` | `0` | Visible timeout in milliseconds, measured from completion of the first successful draw, after InitialDelay or ReshowDelay. Rendering time does not consume the timeout. The default of 0 keeps it open until Hide or dismissal; content updates do not restart the timeout. |
| `Position` | `Auto` | Auto, Below, Above, Left, Right, Absolute, or a pair: Below \| Left, Below \| Right, Above \| Left, Above \| Right. Auto starts below and flips above near the monitor's bottom. Pairs select both vertical and horizontal placement relative to the anchor. |
| `OffsetX`, `OffsetY` | `0`, `2` for balloons / `20` for square tooltips | Offset from the anchor in pixels. Mouse-anchored balloons use the cursor's visible bottom edge when below, or top edge when above; the default leaves a small gap before the tail outline. Above/left use the absolute offset as the gap. Explicit offsets override these defaults. |
| `KeepOnScreen` | `1` | Clamps the popup to the anchor monitor's work area, including monitors with negative coordinates. An oversized tooltip may still exceed the work area. |
| `FollowMouse` | `1` | 1 follows the cursor while open; set 0 to leave it at its initial position. With KeepOnScreen=1, balloons using Auto, Above, or Below (including Above/Below Left/Right pairs) flip to the other side near the top/bottom of the screen, attempting to keep the tail pointed at the cursor. Explicit Show x y stays anchored to its skin-relative coordinates instead. |
| `AlwaysTip` | `1` | 1 allows inactive skins; 0 requires the skin to be the foreground root window. |
| `Topmost` | `1` | 1 puts the tooltip in the topmost window group without activating it. Every opening explicitly raises the tooltip within its window group, including repeated and delayed Show commands, so an On desktop skin does not leave a newly shown tooltip behind an application window. This does not give the tooltip keyboard focus or raise the skin. With 0, it still stays above its skin, inherits the skin's topmost status when necessary, and otherwise opens at the front of the normal window group; unrelated topmost windows can still cover it. Redraws preserve window order instead of repeatedly raising the tooltip or its skin. Actual window order is checked on tooltip position changes and every open-window timer tick: if its own skin moves ahead, the tooltip is placed immediately above it in the window order. This also applies when a following tooltip moves from outside the skin onto it with AlwaysOnTop=2 (Stay Topmost). The plugin does not subclass the skin window, allowing Rainmeter to unload it safely from the skin's window procedure. |
| `DismissOnClick` | `1` | A new left/right mouse press dismisses requested/open tooltips. With 0, ordinary clicks do not dismiss them, but opening this skin's context menu always dismisses an open tooltip or cancels a pending display. This includes keyboard- or bang-opened skin menus. Ctrl+Alt selection dismisses tooltips before its selection menu can open. Menu dismissal applies regardless of DismissOnClick; menus belonging to other skins or unrelated windows do not override this setting. |
| `NoFade` | `0` | 1 disables fade-in. |

With Position=Absolute, Show x y gives the popup's top-left position relative to the skin,
except for a balloon with FollowMouse=0, where it anchors the tail. With plain Show,
OffsetX/OffsetY are the absolute top-left coordinates, scaled from pixels. Other positions
treat Show x y as an anchor. Balloons point their tail toward the anchor vertically.
For Auto/Below/Above balloon positioning, the body shifts left to align its tail
with that anchor. Cursor bounds exclude transparent bitmap padding and account for
the cursor hotspot. Explicit Show x y anchors use the supplied skin-relative point.
KeepOnScreen may move the body/tail near a monitor edge.

For example, `Position=Below | Left` places the tooltip below and to the left of the
anchor, while `Position=Above | Right` places it above and to the right. Spaces around
the pipe are optional and values are case-insensitive. Rectangle tooltips place their
entire width on the selected side, with the absolute OffsetX as the horizontal gap.
Balloons place the body on the selected side with their tail aligned to the anchor;
the tail and rounded corner leave a small portion of the window across the anchor.
For pairs, the absolute OffsetX moves the body farther toward the selected side.
With mouse anchors, OffsetY/TailGap retain their usual vertical behavior. Explicit Above/Below pairs do
not automatically flip, except for a balloon tail anchored by Show x y with FollowMouse=0
or a mouse-following balloon with FollowMouse=1;
KeepOnScreen can still clamp them to the monitor's work area.
Invalid pairs log a warning and fall back to Auto.

For a balloon just below and to the right of the cursor:

```ini
Balloon=1
Position=Below
TailPosition=Right
TailGap=4
```

Use `TailPosition=Left` for just left of the cursor or `Center` for directly under
it. TailGap stays in physical pixels regardless of skin scale; OffsetX/OffsetY
are in scaled pixels. Position=Auto may flip above near a monitor's bottom.
Tail settings are reread while the tooltip is open, like the other options.

Escape dismisses requested/open tooltips. The plugin also hides tooltips when the owning skin
is hidden/minimized. Popups do not activate or steal focus, and content passes mouse
input through except for its close button. Cleanup destroys the instance's windows and timers
when the measure is finalized/refreshed.

## Source layout and development

Version 2.14.0.0 reorganizes the plugin using the conventions in the Rainmeter4 source
and C++ Plugin SDK: class declarations in headers, private instance members with `m_`
names, out-of-line implementations, tab indentation, and Allman braces for functions
and control flow. The existing skin options, defaults, commands, and rendering behavior
are retained. The bundled Rainmeter text formatter and PCRE sources remain separate
dependencies.

| File | Responsibility |
| --- | --- |
| `PluginToolTip.cpp` | Rainmeter SDK exports; delegates to one `ToolTipMeasure` per measure instance. |
| `ToolTipMeasure.h`, `ToolTipMeasure.cpp` | Private instance state, window lifetime, commands, timers, dynamic updates, dismissal, and ordering above the owning skin. |
| `ToolTipLayout.cpp` | DPI scale, text bounds, title-relative coordinates, cursor attachment, window placement, and close-button hit testing. |
| `ToolTipDrawing.cpp` | Tooltip background, gradients, outline geometry, title/body drawing, and layered-window presentation. |
| `ToolTipBevel.cpp` | Local outline lighting, bevel profiles, repeated rims, and the per-instance lighting cache. |
| `ToolTipGraphics.h`, `ToolTipGraphics.cpp` | Shared graphics device resources and creation of each instance's drawing context. |
| `ToolTipIcon.cpp` | Standard icons, icon files, and SVG loading/rasterization. |
| `ToolTipImage.cpp` | Background image decoding, crop origins, GreyScale/tint, flip/rotation, and per-instance caching. |
| `ToolTipOptions.h`, `ToolTipOptions.cpp` | Parsed option snapshot, grouped option readers, measure substitutions, and text-format configuration. |
| `ToolTipParser.h`, `ToolTipParser.cpp` | Colors, gradient stops, inline settings, and bounded PCRE matching. |
| `CursorAttachment.h`, `CursorAttachment.cpp` | Visible cursor bounds relative to its hotspot. |
| `ToolTipRenderer.cpp` | Adapter for the bundled Rainmeter DirectWrite text formatter. |

Build `PluginToolTip.vcxproj` in `Release|Win32` and `Release|x64`. Output DLLs are
`x32/Release/ToolTip.dll` and `x64/Release/ToolTip.dll`. Each measure owns its popup and
releases its timers, icons, layouts, and drawing context during finalization. Measures
share the graphics device and font factory until the final instance is destroyed.
The plugin never subclasses the skin window; Rainmeter can unload a plugin while its
own window procedure is running.

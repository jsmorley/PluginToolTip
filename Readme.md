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

`Linear` preserves the original flat bevel. `Curved` softens the shading toward
the stroke center, `Concave` creates a recessed groove, and `Bulged` creates a
ridge with opposite slopes. These profiles shade contour bands across the border,
including rounded balloon corners and tails. Wider strokes make the profiles more
visible. Raised/Sunken reverses each profile's lighting.

These controls are inspired by Microsoft's [BevelBitmapEffect](https://learn.microsoft.com/en-us/dotnet/api/system.windows.media.effects.bevelbitmapeffect).
They are native Direct2D border effects with their own rendering and defaults;
they do not apply a bitmap bevel to the tooltip background, text, or icons.

## Commands and measure values

| [SP]20[/SP]Command[SP]20[/SP] | Result |
| --- | --- |
| `Show` | Opens at the mouse position after the configured delay. |
| `Show x y` | Opens using an explicit anchor in physical desktop/screen pixels; negative coordinates are supported. |
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

Color options accept `R,G,B`, `R,G,B,A`, six-digit `RRGGBB`, or eight-digit `RRGGBBAA` hex values.
Hex colors may also have a leading `#`, for example `#ffffff` or `#00000080`.

For a white-to-black background gradient, set `SolidColor=#ffffff`,
`SolidColor2=#000000`, and optionally `GradientAngle=90` for top to bottom.
Lengths below are skin units; the plugin uses the skin's effective DPI/zoom scale when supported
by the Rainmeter API. Explicit `Show x y` coordinates are always physical screen pixels.

| [SP]20[/SP]Option[SP]20[/SP] | Default | Meaning |
| --- | --- | --- |
| `SolidColor` | `255,255,255,255` | Background for the **entire tooltip**, including title/icon area, padding, and balloon tail. Alpha is supported. |
| `SolidColor2` | unset | Optional second background color for a gradient over the whole tooltip. |
| `GradientAngle` | `0` | Background gradient angle in degrees; 0 goes left to right, 90 top to bottom. |
| `StrokeSize` | `1` | Outer outline width in skin units, including the balloon tail. Fractional values such as `0.5`, `1.25`, and `2.5` are supported; `0` disables the outline. Clamped to 0–32. |
| `StrokeColor` | `110,110,110,255` | Color of the outer outline. |
| `StrokeBevel` | `None` | `None` uses StrokeColor throughout. `Raised` uses a light top/left and dark bottom/right border; `Sunken` reverses them. Applies to rectangle and balloon outlines, including the tail. Values are case-insensitive. |
| `StrokeBevelLightColor` | derived from StrokeColor | Bevel highlight color. Defaults to StrokeColor blended 45% toward white, preserving its alpha. |
| `StrokeBevelDarkColor` | derived from StrokeColor | Bevel shadow color. Defaults to StrokeColor blended 45% toward black, preserving its alpha. |
| `StrokeBevelProfile` | `Linear` | `Linear`, `Curved`, `Concave`, or `Bulged`. Case-insensitive; invalid values log a warning and use Linear. |
| `StrokeBevelWidth` | StrokeSize | Width of the shaded band, centered within the outline, in skin units. Supports fractions and is clamped to 0–StrokeSize. The remainder uses StrokeColor; 0 gives a flat outline. |
| `StrokeBevelRelief` | `1` | Shading strength from 0 to 1; 0 gives a flat StrokeColor outline, 1 uses the full highlight/shadow colors. |
| `StrokeBevelSmoothness` | `0` | Highlight-to-shadow transition softness from 0 (sharp join) to 1 (broad blend). |
| `StrokeBevelLightAngle` | `225` | Light source direction in degrees clockwise: 0 right, 90 bottom, 180 left, 270 top. 225 lights the top/left. Angles wrap every 360 degrees. |
| `W`, `H` | automatic | Fixed text content width/height. Padding, title/icon row, stroke and tail are added outside the text content. |
| `Padding` | `8,6,8,6` | Left, top, right, bottom padding around the contents. |
| `Title` | empty | Optional heading using the selected font face and TitleFontSize in bold, with matching inline formatting applied. |
| `TitleFontSize` | `FontSize` | Base heading font size, independent of the body text. Matching inline Size settings override it. Updated while open, including the space reserved for the heading. |
| `TitleColor` | `FontColor` | Heading color, separate from the body text. Supports the same RGB/RGBA and hex formats as other colors, including a leading `#`. Unset or invalid values use the current FontColor. Updated while open. |
| `TitleX`, `TitleY` | automatic | Title text origin measured from the tooltip window's top-left corner, in skin units. Set either axis independently; unset/empty axes retain automatic placement after padding, stroke, icon and any top tail. |
| `Icon` | `None` | `Info` / `Information`, `Warning`, `Error`, `Question`, an `.ico` or `.svg` file path relative to the skin, or inline SVG markup. |
| `IconW`, `IconH` | `24`, `24` | Icon width and height in skin units (1–512). The header reserves this space. SVGs are rasterized at these dimensions multiplied by the skin scale. |
| `Balloon` | `0` | `1` adds rounded balloon corners and a tail. `0` draws a square rectangle. |
| `TailPosition` | `Center` | `Left` places the tail just left of the visible cursor, `Center` directly beneath it, and `Right` just right of it. Applies to mouse-anchored balloons in Auto/Below/Above positions. Case-insensitive; invalid values use Center. |
| `TailGap` | unset | Gap in physical screen pixels from the cursor's visible bottom to the popup's tail edge when below (top edge when above). Overrides OffsetY for vertical balloon placement. 0 touches the edge; values are rounded to whole pixels and clamped to 0–8192. Unset preserves OffsetY. Explicit Show x y uses the supplied point instead of cursor bounds. |
| `CornerRadius` | `6` | Balloon corner radius. |
| `CloseButton` | `0` | `1` adds a clickable close button to the formatted heading row. |
| `CloseButtonX`, `CloseButtonY` | automatic | Top-left corner of the close button's 22 by 22 clickable area, measured from the tooltip window's top-left corner in skin units. The X glyph is drawn inside that area. Requires CloseButton=1; unset/empty axes retain automatic placement. |
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
| `ClipString` | `2` | 0: no wrap/ellipsis; 1: wrap/trim to W/H; 2: auto size up to optional maximums, wrapping/trimming as necessary. |
| `ClipStringW`, `ClipStringH` | unset | Automatic maximum text width/height with ClipString=2; ignored when W/H respectively are set. |
| `TrailingSpaces` | `0` | 1 preserves leading/trailing spaces in the text returned by Rainmeter. |
| `Prefix`, `Postfix` | empty | Text before/after Text. |
| `NumOfDecimals` | `0` (1 with AutoScale or a decimal Scale) | Decimal precision for bound numeric measures, from 0 to 16. Percentual defaults to 0. |
| `Scale` | `1` | Divides bound numeric values; AutoScale takes precedence. |
| `AutoScale` | `0` | 0, 1 (1024), 1k, 2 (1000), 2k; suffixes k/M/G/T/P/E. |
| `Percentual` | `0` | 1 uses the bound measure's percentage of its Rainmeter range. |

The tooltip is a separate popup rather than a meter. Its W/H describe the content box and
StringAlign describes alignment inside that box. General meter placement, transformations,
containers, meter actions, and meter groups are not tooltip layout options. Shadows can extend outside the calculated layout; provide sufficient padding/size.

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
appears in the header with or without `Title`. Transparency is preserved.
These dimension options also resize standard and ICO icons.

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

Title and close-button coordinates accept formulas and fractional values and scale with the skin.
They are absolute within the entire tooltip window, including the tail area when a balloon's
tail is on top. Changing coordinates moves the item without changing the normal header/body
layout or automatically expanding the window; use padding, W/H or font sizes to reserve space.
The close button's clickable area follows its new position. All four coordinates update while
the tooltip is open. For example:

```ini
TitleFontSize=16
TitleX=20
TitleY=24
CloseButton=1
CloseButtonX=300
CloseButtonY=20
```

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

Supported settings follow the Rainmeter String meter syntax:

| [SP]20[/SP]Setting[SP]20[/SP] | Example |
| --- | --- |
| Face, Size, Color, Weight | `Face | Segoe UI`, `Size | 14.5`, `Color | FF8844FF`, `Weight | 700` |
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
| `InitialDelay` | `500` | First display delay in milliseconds. |
| `ReshowDelay` | `100` | Delay when reopened within one second of its last hide. |
| `AutoPopDelay` | `5000` | Visible timeout in milliseconds; 0 keeps it open until Hide or dismissal. |
| `Position` | `Auto` | Auto, Below, Above, Left, Right, Absolute. Auto starts below and flips above near the monitor's bottom. |
| `OffsetX`, `OffsetY` | `0`, `2` for balloons / `20` for square tooltips | Offset from the anchor in skin units. Mouse-anchored balloons use the cursor's visible bottom edge when below, or top edge when above; the default leaves a small gap before the tail outline. Above/left use the absolute offset as the gap. Explicit offsets override these defaults. |
| `KeepOnScreen` | `1` | Clamps the popup to the anchor monitor's work area, including monitors with negative coordinates. An oversized tooltip may still exceed the work area. |
| `FollowMouse` | `0` | 1 follows the cursor while open. Explicit Show x y remains fixed. |
| `AlwaysTip` | `1` | 1 allows inactive skins; 0 requires the skin to be the foreground root window. |
| `Topmost` | `1` | 1 keeps the tooltip above other windows without activating it. |
| `DismissOnClick` | `1` | A new left/right mouse press dismisses requested/open tooltips. |
| `NoFade` | `0` | 1 disables fade-in. |

With Position=Absolute, Show x y gives the popup's top-left screen position. With plain Show,
OffsetX/OffsetY are the absolute top-left coordinates, scaled from skin units. Other positions
treat Show x y as an anchor. Balloons point their tail toward the anchor vertically.
For Auto/Below/Above balloon positioning, the body shifts left to align its tail
with that anchor. Cursor bounds exclude transparent bitmap padding and account for
the cursor hotspot. Explicit Show x y anchors remain the supplied screen point.
KeepOnScreen may move the body/tail near a monitor edge.

For a balloon just below and to the right of the cursor:

```ini
Balloon=1
Position=Below
TailPosition=Right
TailGap=4
```

Use `TailPosition=Left` for just left of the cursor or `Center` for directly under
it. TailGap stays in physical pixels regardless of skin scale; OffsetX/OffsetY
are in scaled skin units. Position=Auto may flip above near a monitor's bottom.
Tail settings are reread while the tooltip is open, like the other options.

Escape dismisses requested/open tooltips. The plugin also hides tooltips when the owning skin
is hidden/minimized. Popups do not activate or steal focus, and content passes mouse
input through except for its close button. Cleanup destroys the instance's windows and timers
when the measure is finalized/refreshed.

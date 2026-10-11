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

## Commands and measure values

| Command | Result |
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

| Option | Default | Meaning |
| --- | --- | --- |
| `SolidColor` | `255,255,255,255` | Background for the **entire tooltip**, including title/icon area, padding, and balloon tail. Alpha is supported. |
| `SolidColor2` | unset | Optional second background color for a gradient over the whole tooltip. |
| `GradientAngle` | `0` | Background gradient angle in degrees; 0 goes left to right, 90 top to bottom. |
| `StrokeSize` | `1` | Outer outline width, including the balloon tail; `0` disables the outline. |
| `StrokeColor` | `110,110,110,255` | Color of the outer outline. |
| `W`, `H` | automatic | Fixed text content width/height. Padding, title/icon row, stroke and tail are added outside the text content. |
| `Padding` | `8,6,8,6` | Left, top, right, bottom padding around the contents. |
| `Title` | empty | Optional heading using the selected font face/size in bold. |
| `Icon` | `None` | `Info` / `Information`, `Warning`, `Error`, `Question`, an `.ico` or `.svg` file path relative to the skin, or inline SVG markup. |
| `IconW`, `IconH` | `24`, `24` | Icon width and height in skin units (1–512). The header reserves this space. SVGs are rasterized at these dimensions multiplied by the skin scale. |
| `Balloon` | `0` | `1` adds rounded balloon corners and a tail. `0` draws a square rectangle. |
| `CornerRadius` | `6` | Balloon corner radius. |
| `CloseButton` | `0` | `1` adds a clickable close button to the formatted heading row. |
| `FontFace` | `Arial` | Font family. Installed fonts and fonts under `@Resources\Fonts` are supported. |
| `LocalFont`, `LocalFont2`, ... | unset | Additional skin-relative font files loaded when Show opens the tooltip. |
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
containers, meter actions, and meter groups are not tooltip layout options. Angled text and
shadows can extend outside the calculated layout; provide sufficient padding/size.

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

## Inline string formatting

Use consecutive `InlineSetting`, `InlineSetting2`, etc., with matching `InlinePattern` options.
An omitted pattern defaults to `.*`. PCRE matching, lookarounds, Unicode, and capture groups are
supported. Captures receive the formatting when the pattern contains captures; otherwise the
whole match receives it. Patterns apply after bound values, prefix/postfix and StringCase.

Supported settings follow the Rainmeter String meter syntax:

| Setting | Example |
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
logged and skipped. Up to 256 bindings, inline settings, and local fonts are supported per instance.

## Behavior and positioning

| Option | Default | Meaning |
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

Escape dismisses requested/open tooltips. The plugin also hides tooltips when the owning skin
is hidden/minimized. Popups do not activate or steal focus, and content passes mouse
input through except for its close button. Cleanup destroys the instance's windows and timers
when the measure is finalized/refreshed.

	#define	BORDER	     "|none|hidden|dotted|dashed|solid|double|groove|ridge|inset|outset"
	#define	BORDER_WIDTH "POS"
	#define	AUTO         "3"
	#define AUTOPOS      "|auto="AUTO"|POS"
	#define INHERIT(x,y) (x | (y<<2))

	/*
	 * table defining CSS attributes supported by this library. Fields have following meaning:
	 * - attr: lowercase CSS attribute name.
     *
	 * - format: accepted values by attribute name. Accept following token:
	 *     - '|': enumeration of possible values
	 *     - '-': will use format of another field, located n slots behind the
	 *            field where it is specified (or after if n is negative).
	 *     - ' ': will match any number of CSS spaces in the value.
	 *     - COLOR: color name or color value (rgb(r,g,b), hsl() or #xxx{xxx})
	 *     - POS: css position (number with unit).
	 *     - INT: signed integer (no unit).
	 *     - DBL: real (no unit).
	 *     - STR: css string.
	 *     - URI: url(string).
	 *     - GRAD: linear-gradient(), repeating-linear-gradient()
	 *     - remaining is match as-is (case insensitive).
	 *
	 * - defval: initial value of attribute. Styles are initialy memset() with 0,
	 *       so if this is the default value, simply specify NULL. Otherwise it
	 *       should specify a signed integer encoded as string.
	 *
	 * - inherit: 0 not inherited, 1 inherited, 2 inherited relative (change rel value into abs)
	 *
	 * - arg1 ... arg5: offset within style struct, use OFF macro. Do not include
	 *                  offsets for relative fields.
	 */

	/*
	 * Extensions:
	 * - -bg-transform: apply transformation to background-image without the needs of pseudo elements
	 * - -caret-color: color of caret for text box
	 * - -outline-offset: offset (single value) to push the outline border (by default: outside border box).
	 * - -selection: color of selection for bg + fg, default: operating system colors
	 */

	#define	OFF(field)      (int) &((SIT_Widget)0L)->style.field
	#define	OFFBG(field)    (int) &((Background)0L)->field
	#define OFFSH(field)    (int) &((TextShadow)0L)->field
	#define OFFBOX(field)   (int) &((BoxShadow)0L)->field
	#define OFFLIST(field)  (int) &((SIT_ListBox)0L)->field
	#define OFFBI(field)    (int) &((BorderImg)0L)->field
	#define OFFREAL(field)  (int) &((SIT_Widget)0L)->layout.field

	/* dummy properties for background-position (need defval) */
	{.defval  = AUTO,                    .format  = "|auto=3|left=0|center=50%|right=100%|POS",
	 .arg1    = OFFBG(x),                .objtype = 1},

	{.defval  = AUTO,                    .format = "|auto=3|top=0|center=50%|bottom=100%|POS",
	 .arg1    = OFFBG(y),                .objtype = 1},

	{.attr    = "background-clip",       .format = "|border-box|padding-box|content-box",
	 .arg1    = OFFBG(clip),             .inherit = 0, .sz = 1, .objtype = 1},

	{.attr    = "background-attachment", .format  = "|scroll|fixed",
	 .defval  = NULL,                    .inherit = 0, .sz = 1, .objtype = 1,
	 .arg1    = OFFBG(attachment)},

	{.attr    = "background-color",      .format  = "COLOR",
	 .defval  = NULL,                    .inherit = INHERIT(0, cssAttrBgColor), .sz = 4, .objtype = 1,
	 .arg1    = OFFBG(color)},

	{.attr    = "background-image",      .format  = "|none|URI|GRAD",
	 .defval  = NULL,                    .inherit = INHERIT(0, cssAttrBgImage), .objtype = 1,
	 .arg1    = OFFBG(uriBg)},

	{.attr    = "background-position",   .format  = "-6 -5", .objtype = 1, .reflow = ReflowBgPos, .inherit = INHERIT(0, cssAttrBgPos)},

	{.attr    = "background-repeat",     .format  = "|repeat|repeat-x|repeat-y|no-repeat",
	 .defval  = NULL,                    .inherit = 0, .sz = 1, .objtype = 1,
	 .arg1    = OFFBG(repeat)},

	{.attr    = "background-size",       .format  = "|auto=0|cover=7|contain=11|POS |auto=0|POS",
	 .defval  = NULL,                    .inherit = 0, .objtype = 1,
	 .arg1    = OFFBG(bgw),              .arg2    = OFFBG(bgh)},

	/* sum-up for previous 5 properties: COLOR:5 IMG:4 REPEAT:2 ATTACH:6 POS:3 CLIP:7 */
	{.attr    = "background",            .format  = "-5 -4 -2 -6 -3 -7",
	 .defval  = NULL,                    .inherit = INHERIT(0, cssAttrBackground), .objtype = 1},

	/* non standard */
	{.attr    = "-bg-transform",         .format  = "STR",
	 .defval  = NULL,                    .inherit = INHERIT(0, cssAttrBgTransform), .objtype = 1,
	 .arg1    = OFFBG(transform)},

	/* modulate background-image with this color */
	{.attr    = "-bg-mod-color",         .format  = "|auto=0|COLOR",
	 .defval  = NULL,                    .inherit = INHERIT(0, cssAttrBgModColor), .objtype = 1,
	 .arg1    = OFFBG(gradient.colors)},

	/* border-radius related properties */
	{.attr    = "border-top-left-radius",     .format  = BORDER_WIDTH " " BORDER_WIDTH,
	 .defval  = NULL,                         .get     = OFFREAL(majorRadius.top),
	 .arg1    = OFF(majorRadius.top),         .arg2    = OFF(minorRadius.top)},

	{.attr    = "border-top-right-radius",    .format  = BORDER_WIDTH " " BORDER_WIDTH,
	 .defval  = NULL,                         .get     = OFFREAL(majorRadius.right),
	 .arg1    = OFF(majorRadius.right),       .arg2    = OFF(minorRadius.right)},

	{.attr    = "border-bottom-right-radius", .format  = BORDER_WIDTH " " BORDER_WIDTH,
	 .defval  = NULL,                         .get     = OFFREAL(majorRadius.bottom),
	 .arg1    = OFF(majorRadius.bottom),      .arg2    = OFF(minorRadius.bottom)},

	{.attr    = "border-bottom-left-radius",  .format  = BORDER_WIDTH " " BORDER_WIDTH,
	 .defval  = NULL,                         .get     = OFFREAL(majorRadius.left),
	 .arg1    = OFF(majorRadius.left),        .arg2    = OFF(minorRadius.left)},

	{.attr    = "border-radius",         .format  = BORDER_WIDTH " " BORDER_WIDTH " " BORDER_WIDTH " " BORDER_WIDTH,
	 .defval  = NULL,                    .inherit = INHERIT(0, cssAttrBorderRadius),
	 .arg1    = OFF(majorRadius.top),    .arg2    = OFF(majorRadius.right),
	 .arg3    = OFF(majorRadius.bottom), .arg4    = OFF(majorRadius.left)},

	/* this is a dummy property to deal with the minor axis */
	{.attr    = "border-radius-minor",   .format  = BORDER_WIDTH " " BORDER_WIDTH " " BORDER_WIDTH " " BORDER_WIDTH,
	 .defval  = NULL,                    .inherit = 0,
	 .arg1    = OFF(minorRadius.top),    .arg2    = OFF(minorRadius.right),
	 .arg3    = OFF(minorRadius.bottom), .arg4    = OFF(minorRadius.left)},

	/* space between table cells */
	{.attr    = "border-spacing",        .format  = AUTOPOS " POS",
	 .defval  = NULL,                    .inherit = INHERIT(0, cssAttrBorderSpacing), .reflow = ReflowLayout,
	 .arg1    = OFFLIST(borderCSSH),     .arg2    = OFFLIST(borderCSSV)},

	/* border colors */
	{.attr    = "border-top-color",      .format  = "COLOR",     /* warning: field is relative to border, 14 attr below */
	 .defval  = NULL,                    .inherit = 0, .sz = 4,
	 .arg1    = OFF(borderTop.color)},

	{.attr    = "border-right-color",    .format  = "COLOR",
	 .defval  = NULL,                    .inherit = 0, .sz = 4,
	 .arg1    = OFF(borderRight.color)},

	{.attr    = "border-bottom-color",   .format  = "COLOR",
	 .defval  = NULL,                    .inherit = 0, .sz = 4,
	 .arg1    = OFF(borderBottom.color)},

	{.attr    = "border-left-color",     .format  = "COLOR",
	 .defval  = NULL,                    .inherit = 0, .sz = 4,
	 .arg1    = OFF(borderLeft.color)},

	/* 4 border colors */
	{.attr    = "border-color",          .format  = "-4 -3 -2 -1",
	 .defval  = NULL,                    .inherit = 0},

	/* width, style and color of 4 borders */
	{.attr    = "border-top",            .format  = BORDER_WIDTH" --4 -5",
	 .defval  = NULL,                    .inherit = 0, .reflow = ReflowLayout,
	 .arg1    = OFF(borderTop.width)},

	{.attr    = "border-right",          .format  = BORDER_WIDTH" --4 -5",
	 .defval  = NULL,                    .inherit = 0, .reflow = ReflowLayout,
	 .arg1    = OFF(borderRight.width)},

	{.attr    = "border-bottom",         .format  = BORDER_WIDTH" --4 -5",
	 .defval  = NULL,                    .inherit = 0, .reflow = ReflowLayout,
	 .arg1    = OFF(borderBottom.width)},

	{.attr    = "border-left",           .format  = BORDER_WIDTH" --4 -5",
	 .defval  = NULL,                    .inherit = 0, .reflow = ReflowLayout,
	 .arg1    = OFF(borderLeft.width)},

	/* border styles */
	{.attr    = "border-top-style",      .inherit = 0, .sz = 1,
	 .defval  = NULL,                    .format  = BORDER,
	 .arg1    = OFF(borderTop.style)},

	{.attr    = "border-right-style",    .inherit = 0, .sz = 1,
	 .defval  = NULL,                    .format  = BORDER,
	 .arg1    = OFF(borderRight.style)},

	{.attr    = "border-bottom-style",   .inherit = 0, .sz = 1,
	 .defval  = NULL,                    .format  = BORDER,
	 .arg1    = OFF(borderBottom.style)},

	{.attr    = "border-left-style",     .inherit = 0, .sz = 1,
	 .defval  = NULL,                    .format  = BORDER,
	 .arg1    = OFF(borderLeft.style)},

	/* sum-up for previous 4 properties */
	{.attr    = "border-style",          .format  = "-4 -3 -2 -1",
	 .defval  = NULL,                    .inherit = 0, .sz = 1},

	/* 4 borders style, width and color */
	{.attr    = "border",                .inherit = INHERIT(0, cssAttrBorder), .reflow = ReflowLayout,
	 .defval  = NULL,                    .format  = BORDER_WIDTH" -5 -14",
	 .arg1    = OFF(borderTop.width)},

	/* border widths */
	{.attr    = "border-top-width",      .get     = OFFREAL(border.top), .reflow = ReflowLayout,
	 .defval  = NULL,                    .format  = BORDER_WIDTH,
	 .arg1    = OFF(borderTop.width)},

	{.attr    = "border-right-width",    .get     = OFFREAL(border.right), .reflow = ReflowLayout,
	 .defval  = NULL,                    .format  = BORDER_WIDTH,
	 .arg1    = OFF(borderRight.width)},

	{.attr    = "border-bottom-width",   .get     = OFFREAL(border.bottom), .reflow = ReflowLayout,
	 .defval  = NULL,                    .format  = BORDER_WIDTH,
	 .arg1    = OFF(borderBottom.width)},

	{.attr    = "border-left-width",     .get     = OFFREAL(border.left), .reflow = ReflowLayout,
	 .defval  = NULL,                    .format  = BORDER_WIDTH,
	 .arg1    = OFF(borderLeft.width)},

	/* 4 border widths at once */
	{.attr    = "border-width",          .inherit = 0,
	 .defval  = NULL,                    .format  = "-4 -3 -2 -1"},

	/* border-image group */
	{.attr    = "border-image-slice",    .inherit = INHERIT(0, cssAttrBorderImgSlice), .objtype = 4,
	 .defval  = "-1",                    .format  = "INT INT INT INT",
	 .arg1    = OFFBI(slice.top),        .arg2    = OFFBI(slice.right),
	 .arg3    = OFFBI(slice.bottom),     .arg4    = OFFBI(slice.left)},

	{.attr    = "border-image-width",    .inherit = INHERIT(0, cssAttrBorderImgWidth), .objtype = 4,
	 .defval  = "-1",                    .format  = "POS POS POS POS",
	 .arg1    = OFFBI(widthFixed.top),   .arg2    = OFFBI(widthFixed.right),
	 .arg3    = OFFBI(widthFixed.bottom),.arg4    = OFFBI(widthFixed.left)},

	{.attr    = "border-image-outset",   .inherit = INHERIT(0, cssAttrBorderImgOutset), .objtype = 4,
	 .defval  = "-1",                    .format  = "POS POS POS POS",
	 .arg1    = OFFBI(offFixed.top),     .arg2    = OFFBI(offFixed.right),
	 .arg3    = OFFBI(offFixed.bottom),  .arg4    = OFFBI(offFixed.left)},

	{.attr    = "border-image-repeat",   .inherit = INHERIT(0, cssAttrBorderImgRepeat), .objtype = 4, .sz = 1,
	 .defval  = NULL,                    .format  = "|\1|stretch|repeat|round|space |\1|stretch|repeat|round|space",
	 .arg1    = OFFBI(repeatH),          .arg2    = OFFBI(repeatV)},

	{.attr    = "border-image-source",   .inherit = INHERIT(0, cssAttrBorderImgSrc), .objtype = 4, .reflow = ReflowBorderImg,
	 .defval  = NULL,                    .format  = "|none|URI|GRAD",
	 .arg1    = OFFBI(source)},

	/* dummy property because of field size */
	{.arg1    = OFFBI(fill),             .format  = "|\1|fill|content-fill", .sz = 1},

	{.attr    = "border-image",          .inherit = INHERIT(0, cssAttrBorderImg), .objtype = 4,
	 .defval  = NULL,                    .format  = "-2 -6 -1 / -5 / -4 -3"},

	/* box-shadow-color */
	{.attr    = NULL,                    .inherit = 0, .objtype = 3,
	 .defval  = NULL,                    .format  = "COLOR",
	 .arg1    = OFFBOX(color)},

	/* box-shadow */
	{.attr    = "box-shadow",            .objtype = 3, .inherit = INHERIT(0, cssAttrBoxShadow),
	 .defval  = NULL,                    .format  = "POS POS POS POS |\1|inset -1",
	 .arg1    = OFFBOX(XYSfixed[0]),     .arg2    = OFFBOX(XYSfixed[1]),
	 .arg3    = OFFBOX(blurFixed),       .arg4    = OFFBOX(XYSfixed[2]),
	 .arg5    = OFFBOX(inset)},

	/* editbox only */
	{.attr    = "-caret-color",        .inherit = INHERIT(0, cssAttrCaret), .sz = 4,
	 .defval  = NULL,                  .format  = "COLOR",
	 .arg1    = (int) &((SIT_EditBox)0L)->caret},

	/* foreground color (text) */
	{.attr    = "color",               .inherit = 1, .sz = 4,
	 .defval  = NULL,                  .format  = "COLOR",
	 .arg1    = OFF(color)},

	/* :before and :after pseudo-elements only. Ignored on others */
//	{.attr    = "content",             .inherit = 0, .reflow = ReflowLayout,
//	 .defval  = NULL,                  .format  = "STR",
//	 .arg1    = OFF(content)},

	/* inline is the default display for tags */
//	{.attr    = "display",             .inherit = 0, .sz = 1, .reflow = ReflowLayout,
//	 .defval  = "1",                   .format  = "|none|inline|inline-block",
//	 .arg1    = OFF(display)},

	/* font face */
	{.attr    = "font-family",         .inherit = 1, .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = "FONT",
	 .arg1    = OFF(font.family),      .get     = OFF(font.handle)},

	{.attr    = "font-size",           .inherit = INHERIT(2, cssAttrFontSize), .reflow = ReflowLayout,
	 .defval  = "=16pt",               .format  = "|xx-small=6pt|x-small=8pt|small=10pt|medium=14pt|large=20pt"
	                                              "|x-large=28pt|xx-large=32pt|smaller=7|larger=11|POS",
	 .arg1    = OFF(fontSize),         .get     = OFF(font.size)},

	{.attr    = "font-style",          .inherit = 1, .sz = 1, .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = "|normal|italic|oblique",
	 .arg1    = OFF(font.italic)},

	{.attr    = "font-variant",        .inherit = 1, .sz = 1, .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = "|normal|small-caps",
	 .arg1    = OFF(font.variant)},

	{.attr    = "font-weight",         .inherit = INHERIT(2, cssAttrFontWeight), .reflow = ReflowLayout,
	 .defval  = "300",                 .format  = "|normal=300|bold=700|bolder=1|lighter=-1|INT",
	 .arg1    = OFF(font.weight)},

	{.attr    = "font",                .inherit = INHERIT(0, cssAttrFont),
	 .defval  = NULL,                  .format  = "-3 -2 -1 -4 --4 -5"},

	/* only used for <img> tag */
	{.attr    = "height",              .inherit = 0, .reflow = ReflowLayout,
	 .defval  = AUTO,                  .format  = AUTOPOS,
	 .arg1    = OFF(height)},

	/* positioned elements only */
	{.attr    = "left",                .reflow  = ReflowOffset,
	 .defval  = NULL,                  .format  = AUTOPOS,
	 .arg1    = OFF(left),             .get     = OFFREAL(left)},

	{.attr    = "letter-spacing",      .reflow  = ReflowLayout, .inherit = 2,
	 .defval  = AUTO,                  .format  = "|normal="AUTO"|POS",
	 .arg1    = OFF(letterSpacing),    .get     = OFFREAL(letterSpacing)},

	/* refer to font-size */
	{.attr    = "line-height",         .inherit = INHERIT(2, cssAttrLineHeight), .reflow = ReflowLayout,
	 .defval  = AUTO,                  .format  = "|normal="AUTO"|POS",
	 .arg1    = OFF(lineHeight)},

	/* hack(TM): only one component supported */
	{.attr    = "margin",              .get     = OFFREAL(margin), .reflow = ReflowLayout,
	 .arg1    = OFF(margin),           .format  = "POS"},

	#if 0
	/* 4 margins */
	{.attr    = "margin-top",          .inherit = 0, .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = AUTOPOS,
	 .arg1    = OFF(margin.top)},

	{.attr    = "margin-right",        .inherit = 0, .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = AUTOPOS,
	 .arg1    = OFF(margin.right)},

	{.attr    = "margin-bottom",       .inherit = 0, .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = AUTOPOS,
	 .arg1    = OFF(margin.bottom)},

	{.attr    = "margin-left",         .inherit = 0, .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = AUTOPOS,
	 .arg1    = OFF(margin.left)},

	/* constraint on width/height */
	{.attr    = "max-height",          .inherit = 0, .reflow = ReflowLayout,
	 .defval  = AUTO,                  .format  = "|none="AUTO"|POS",
	 .arg1    = OFF(max.height)},

	{.attr    = "max-width",           .inherit = 0, .reflow = ReflowLayout,
	 .defval  = AUTO,                  .format  = "|none="AUTO"|POS",
	 .arg1    = OFF(max.width)},

	{.attr    = "min-height",          .inherit = 0, .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = "POS",
	 .arg1    = OFF(min.height)},

	{.attr    = "min-width",           .inherit = 0, .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = "POS",
	 .arg1    = OFF(min.width)},
	#endif

	{.attr    = "opacity",             .inherit = 0,
	 .defval  = "1",                   .format  = "DBL",
	 .arg1    = OFF(opacity)},

	/* outline - does not change border-box */
	{.attr    = "outline-color",       .inherit = 0, .sz = 4,
	 .defval  = NULL,                  .format  = "COLOR",
	 .arg1    = OFF(outline.color)},

	{.attr    = "outline-style",       .inherit = 0, .sz = 1, .reflow = ReflowOutline,
	 .defval  = NULL,                  .format  = BORDER,
	 .arg1    = OFF(outline.style)},

	{.attr    = "outline-width",       .get     = OFFREAL(outlineWidth), .reflow = ReflowOutline,
	 .defval  = "7",                   .format  = BORDER_WIDTH,
	 .arg1    = OFF(outline.width)},

	/* extension */
	{.attr    = NULL,                  .get     = OFFREAL(outlineOffset), .reflow = ReflowOutline,
	 .defval  = NULL,                  .format  = "POS",
	 .arg1    = OFF(outlineOffset)},

	/* sum-up for previous 4 properties; last arg is an extension useful for debugging */
	{.attr    = "outline",             .inherit = 0, .sz = 1,
	 .defval  = NULL,                  .format  = "-4 -3 -2 -1"},

	/* 'scroll' will be handled as visible in paged media */
	{.attr    = "overflow",            .inherit = 0, .sz = 1,
	 .defval  = NULL,                  .format  = "|visible|hidden|scroll|auto",
	 .arg1    = OFF(overflow)},

	/* break words that does not fit in a entire line (CSS3) */
//	{.attr    = "overflow-wrap",       .inherit = 1, .sz = 1, .reflow = ReflowLayout,
//	 .defval  = NULL,                  .format  = "|normal|break-word",
//	 .arg1    = OFF(overflowWrap)},

	/* 4 padding widths */
	{.attr    = "padding-top",         .get     = OFFREAL(padding.top), .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = "POS",
	 .arg1    = OFF(padding.top)},

	{.attr    = "padding-right",       .get     = OFFREAL(padding.right), .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = "POS",
	 .arg1    = OFF(padding.right)},

	{.attr    = "padding-bottom",      .get     = OFFREAL(padding.bottom), .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = "POS",
	 .arg1    = OFF(padding.bottom)},

	{.attr    = "padding-left",        .get     = OFFREAL(padding.left), .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = "POS",
	 .arg1    = OFF(padding.left)},

	/* sum-up for previous 4 properties */
	{.attr    = "padding",             .inherit = 0, .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = "-4 -3 -2 -1"},

	/* selection color/bg (text) */
	{.attr    = "selection",           .inherit = INHERIT(1, cssAttrSelection), .sz = 4,
	 .defval  = NULL,                  .format  = "COLOR COLOR",
	 .arg1    = OFF(fgSel),            .arg2    = OFF(bgSel)},

	/* CSS3 - only useful with <textarea> */
	{.attr    = "tab-size",            .inherit = 1, .sz = 1, .reflow = ReflowLayout,
	 .defval  = "4",                   .format  = "INT",
	 .arg1    = OFF(tabSize)},

	/* block level */
	{.attr    = "text-align",          .inherit = 1, .sz = 1, .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = "|left|right|center|justify",
	 .arg1    = OFF(text.align)},

	{.attr    = "text-decoration",     .inherit = 0, .sz = 1,
	 .defval  = NULL,                  .format  = "|none|underline|overline|line-through --2 --1",
	 .arg1    = OFF(font.decoration)},

	/* CSS3 */
	{.attr    = "text-decoration-color", .inherit = 0, .sz = 4,
	 .defval  = NULL,                    .format  = "COLOR",
	 .arg1    = OFF(decoColor)},

	/* CSS3 */
	{.attr    = "text-decoration-style", .inherit = 0, .sz = 1,
	 .defval  = NULL,                    .format  = "|solid|double|wavy",
	 .arg1    = OFF(decoStyle)},

	/* XXX unused */
	{.attr    = "text-indent",         .inherit = 2, .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = "POS",
	 .arg1    = OFF(text.indent)},

	/* XXX unused */
	{.attr    = "text-overflow",       .inherit = 0, .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = "STR",
	 .arg1    = OFF(textOverflow)},

	/* CSS3 - should be inheritable */
	{.attr    = "text-shadow",         .format  = "POS POS POS COLOR", .objtype = 2, .inherit = INHERIT(0, cssAttrTextShadow),
	 .arg1    = OFFSH(pos.XYfixed[0]), .arg2    = OFFSH(pos.XYfixed[1]),
	 .arg3    = OFFSH(blurFixed),      .arg4    = OFFSH(color)},

	{.attr    = "text-transform",      .inherit = 1, .sz = 1, .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = "|none|capitalize|uppercase|lowercase",
	 .arg1    = OFF(text.transform)},

	/* positioned elements */
	{.attr    = "top",                 .get     = OFFREAL(top), .reflow = ReflowOffset,
	 .defval  = NULL,                  .format  = AUTOPOS,
	 .arg1    = OFF(top)},

	/* CSS3 2D transformations */
	{.attr    = "transform",           .format = "STR", /* will be parsed in SIT_Render.c */
	 .arg1    = OFF(transform),        .inherit = INHERIT(0, cssAttrTransform)},

	/* dummy properties for transform-origin */
	{.defval  = "=50%",                .format = "|left=0|right=100%|center=50%|POS",
	 .arg1    = OFF(transformX)},

	{.defval  = "=50%",                .format = "|top=0|bottom=100%|center=50%|POS",
	 .arg1    = OFF(transformY)},

	{.attr    = "transform-origin",    .inherit = 0,
	 .defval  = NULL,                  .format  = "-2 -1"},

	/* 'inline' and 'table-cell' elements */
	{.attr    = "vertical-align",      .inherit = 1, .reflow = ReflowLayout,
	 .defval  = "7",                   .format  = "|baseline=7|sub=11|super=15|top=19|text-top=23"
	                                              "|middle=27|bottom=31|text-bottom=35|POS",
	 .arg1    = OFF(verticalAlign)},

	{.attr    = "white-space",         .inherit = 1, .sz = 1, .reflow = ReflowLayout,
	 .defval  = NULL,                  .format  = "|normal|nowrap|pre|pre-line|pre-wrap",
	 .arg1    = OFF(whiteSpace)},

	/* only used for <img> tag */
	{.attr    = "width",               .reflow  = ReflowLayout,
	 .defval  = AUTO,                  .format  = AUTOPOS,
	 .arg1    = OFF(width)},

	{.attr    = "word-spacing",        .reflow  = ReflowLayout, .inherit = 2,
	 .defval  = AUTO,                  .format  = "|normal="AUTO"|POS",
	 .arg1    = OFF(wordSpacing),      .get     = OFFREAL(wordSpacing)},

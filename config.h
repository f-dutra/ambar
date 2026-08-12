static char bgnorm[] = "#000000";
static char fgnorm[] = "#eeeeee";
static char bgsel[] = "#222222";
static char fgsel[] = "#ffffff";
static char bordernorm[]	= "#555555";
static char bordersel[]	= "#6a6a6a";
static char fontname[] = "monospace";
static double fontsize = 16.00;
static int margin = 6;
static int lrpad = 14;

static char *colors[SchemeLast][3] = {
	[SchemeNorm] = { fgnorm, bgnorm, bordernorm },
	[SchemeSel]  = { fgsel, bgsel, bordersel  },
};


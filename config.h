void cfg_cleanup(void);
void cfg_load(void);
void cfg_load_xresources(void);
void cfg_reload(void);
void cfg_watch_init(void);
int  cfg_watch_getfd(void);
int  cfg_watch_handle(void);
void cfg_watch_cleanup(void);

extern int margin;
extern int marginvert;
extern int padding;
extern int lrpad;
extern int moduleradius;
extern int height;

extern int workspaceselw;
extern int workspaceselh;
extern int workspacepx;
extern int workspacelrpad;
extern int workspacepadding;

extern char bgnorm[];
extern char bgsel[];
extern char bghover[];

extern char fgnorm[];
extern char fgsel[];
extern char fghover[];

extern char wsbgnorm[];
extern char wsfgnorm[];
extern char wsfgsel[];

extern char bordernorm[];
extern char bordersel[];
extern char borderhover[];

extern char fontname[];
extern double fontsize;

extern Module *modules;
extern int nmodules;


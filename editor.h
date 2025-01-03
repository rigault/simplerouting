typedef void  (*Callback) (void *);
extern        GtkApplicationWindow *windowEditor;
extern bool   editor (GtkApplication *app, const char *fileName,  Callback callback);

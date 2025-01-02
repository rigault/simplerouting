typedef void (*Callback) (void *);
extern GtkApplicationWindow *windowEditor;
bool editor (GtkApplication *app, const char *fileName,  Callback callback);

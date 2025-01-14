typedef void  (*Callback) (void *);
extern        GtkApplicationWindow *windowEditor;
extern bool   myEditor(GtkApplication *app, const char **fileNames, int fileCount, const char *title, Callback callback);


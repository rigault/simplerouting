#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <stdbool.h>
#include <stdio.h>
typedef void (*Callback) (void *);
GtkApplicationWindow *windowEditor;

/*! type to exchange data linked to editor */
typedef struct {
   GtkWidget *win;
   GtkSourceBuffer *buffer;
   GtkWidget *searchEntry;
   gint currentMatch;
   GList *matches;
   char *fileName;
   char *str;
} EditorData;

/*! copy button for editor */
static void onCopyClicked (GtkButton *button, gpointer user_data) {
   EditorData *data = (EditorData *) user_data;
   GdkDisplay *display = gdk_display_get_default ();
   GdkClipboard *clipboard = gdk_display_get_clipboard (display);
   gdk_clipboard_set_text (clipboard, data->str);
}

/*! select all string matching text  */
static void searchAndHighlight(GtkSourceBuffer *buffer, const gchar *text, GList **matches) {
   GtkTextIter startIter, endIter;
   gchar *content;

   // Nettoyer les précédentes correspondances
   *matches = NULL;

   // Récupérer la table des tags
   GtkTextTagTable *tagTable = gtk_text_buffer_get_tag_table(GTK_TEXT_BUFFER(buffer));

   // Chercher si le tag "highlightYellow" existe déjà
   GtkTextTag *highlightTag = gtk_text_tag_table_lookup(tagTable, "highlightYellow");

   // Si le tag n'existe pas, on le crée
   if (highlightTag == NULL) {
      highlightTag = gtk_text_buffer_create_tag(GTK_TEXT_BUFFER(buffer), "highlightYellow", "background", "yellow", NULL);
   }

   if (highlightTag == NULL) {
      fprintf(stderr, "In searchAndHighlight, Impossible to get tag for highlightYellow");
      return;
   }

   // Définir les itérateurs aux bornes du buffer
   gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(buffer), &startIter, &endIter);

   // Supprimer les tags de surbrillance précédents si le tag existe
   if (GTK_IS_TEXT_TAG(highlightTag)) {
      gtk_text_buffer_remove_tag(GTK_TEXT_BUFFER(buffer), highlightTag, &startIter, &endIter);
   }

   // Obtenir le contenu du texte
   content = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(buffer), &startIter, &endIter, FALSE);

   // Convertir les deux chaînes (texte et contenu) en minuscules pour rendre la recherche insensible à la casse
   gchar *lowercaseSearchText = g_utf8_strdown(text, -1);
   gchar *lowercaseContent = g_utf8_strdown(content, -1);

   // Trouver toutes les correspondances du texte (insensible à la casse)
   const gchar *current_pos = lowercaseContent;
   while ((current_pos = g_strstr_len(current_pos, -1, lowercaseSearchText)) != NULL) {
      GtkTextIter matchStartIter, matchEndIter;

      // Calculer l'offset en caractères UTF-8
      gint start_offset = g_utf8_pointer_to_offset(lowercaseContent, current_pos);
      gint end_offset = start_offset + g_utf8_strlen(lowercaseSearchText, -1);

      // Obtenir les itérateurs aux positions calculées
      gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(buffer), &matchStartIter, start_offset);
      gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(buffer), &matchEndIter, end_offset);

      // Ajouter la correspondance à la liste
      *matches = g_list_prepend(*matches, g_strdup(current_pos));

      // Appliquer le tag de surbrillance
      if (GTK_IS_TEXT_TAG(highlightTag)) {
         gtk_text_buffer_apply_tag(GTK_TEXT_BUFFER(buffer), highlightTag, &matchStartIter, &matchEndIter);
      }

      // Continuer à chercher à partir de la fin de la correspondance trouvée
      current_pos = g_utf8_next_char(current_pos);
   }

   // Libérer la mémoire allouée pour les chaînes minuscules
   g_free(lowercaseSearchText);
   g_free(lowercaseContent);
   g_free(content);
}

/*! Manage the search for editor */
static void onSearchClicked (GtkButton *button, gpointer user_data) {
   EditorData *data = (EditorData *)user_data;
   const gchar *searchText = gtk_editable_get_text(GTK_EDITABLE(data->searchEntry));

   if (!searchText || strlen (searchText) == 0) {
      return;
   }

   // Réinitialiser les correspondances et l'état de recherche
   data -> currentMatch = 0;
   g_list_free (data->matches);  // Libérer les anciennes correspondances
   data->matches = NULL;

   // Chercher et surligner les correspondances
   searchAndHighlight (data->buffer, searchText, &data->matches);
}

/*! save the file with modification */
static void onSaveCkicked (GtkButton *button, gpointer user_data) {
   EditorData *data = (EditorData *)user_data;
   GtkTextIter start, end;
   gchar *text;

   gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER(data->buffer), &start, &end);
   text = gtk_text_buffer_get_text (GTK_TEXT_BUFFER(data->buffer), &start, &end, FALSE);

   FILE *file = fopen (data->fileName, "w");
   if (file) {
      fputs (text, file);
      fclose (file);
   } else {
      fprintf (stderr, "In onSaveCkicked, Impossible to save file: %s", data->fileName);
   }
   g_free (text);
}

/*! color first line in red bold */
void applySyntaxHighlight(GtkSourceBuffer *sourceBuffer) {
   GtkTextTag *tag = gtk_text_buffer_create_tag(GTK_TEXT_BUFFER (sourceBuffer),
                                                 "highlightRed",  // Nom de la balise
                                                 "foreground", "red",  // Texte en rouge
                                                 "weight", PANGO_WEIGHT_BOLD,  // Texte en gras
                                                 NULL);

   GtkTextIter start, end;
   gtk_text_buffer_get_iter_at_line (GTK_TEXT_BUFFER (sourceBuffer), &start, 0);  // Début de la première ligne
   gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER(sourceBuffer), &end, 0, 
                                           gtk_text_iter_get_chars_in_line(&start));  // Fin de la première ligne

   gtk_text_buffer_apply_tag (GTK_TEXT_BUFFER (sourceBuffer), tag, &start, &end);
}

/*! simple test editor with finder, copy and save buttons */
bool editor (GtkApplication *app, const char *fileName,  Callback callback) {
   FILE *file = fopen (fileName, "r");
   if (file == NULL) {
      fprintf (stderr, "In editor, Impossible to open: %s", fileName);
      return false;
   }

   // read content of the file
   fseek (file, 0, SEEK_END);
   size_t fileSize = ftell(file);
   rewind (file);
   EditorData *data = g_new0 (EditorData, 1);

   char *content = g_malloc(fileSize + 1);
   size_t readSize = fread (content, 1, fileSize, file);
   if (readSize != fileSize) {
      fprintf (stderr, "In editor, Error reading file: %s", fileName);
      g_free (content);
      fclose (file);
      return false;
   }
   content [fileSize] = '\0';
   data->str = g_strdup (content);
   fclose (file);

   // Main window creation
   windowEditor = GTK_APPLICATION_WINDOW (gtk_application_window_new(app));
   gtk_window_set_default_size (GTK_WINDOW (windowEditor), 800, 600);
   gtk_window_set_title (GTK_WINDOW (windowEditor), fileName);

   // GtkSourceView with buffer creation
   GtkWidget *sourceView = gtk_source_view_new ();
   GtkSourceBuffer *sourceBuffer = gtk_source_buffer_new (NULL);
   gtk_text_buffer_set_text (GTK_TEXT_BUFFER (sourceBuffer), content, -1);
   gtk_text_view_set_buffer (GTK_TEXT_VIEW (sourceView), GTK_TEXT_BUFFER (sourceBuffer));
   gtk_text_view_set_monospace (GTK_TEXT_VIEW (sourceView), TRUE);
   
   // Appliquer la coloration syntaxique à la première ligne
   applySyntaxHighlight (sourceBuffer);

   // Scroll bar creation
   GtkWidget *scroll = gtk_scrolled_window_new ();
   gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), sourceView);
   gtk_widget_set_hexpand (scroll, TRUE);
   gtk_widget_set_vexpand (scroll, TRUE);

   // Tool bar creation
   GtkWidget *toolbar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
   GtkWidget *saveButton = gtk_button_new_with_label ("Save");
   gtk_box_append (GTK_BOX (toolbar), saveButton);
   GtkWidget *searchEntry = gtk_entry_new ();
   gtk_box_append (GTK_BOX (toolbar), searchEntry);

   g_object_set_data (G_OBJECT (searchEntry), "buffer", sourceBuffer);

   GtkWidget *searchClicButton = gtk_button_new_from_icon_name ("edit-find");
   GtkWidget *pasteButton = gtk_button_new_from_icon_name ("edit-copy");
   gtk_box_append (GTK_BOX (toolbar), searchClicButton);
   gtk_box_append (GTK_BOX (toolbar), pasteButton);

   GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   gtk_box_append (GTK_BOX(box), toolbar);
   gtk_box_append (GTK_BOX(box), scroll);

   data->buffer = sourceBuffer;
   data->searchEntry = searchEntry;
   data->fileName = g_strdup (fileName);
   data->matches = NULL;
   data->currentMatch = 0;
   //data->win = window;

   g_signal_connect (saveButton, "clicked", G_CALLBACK (onSaveCkicked), data);
   g_signal_connect (searchClicButton, "clicked", G_CALLBACK (onSearchClicked), data);
   g_signal_connect (pasteButton, "clicked", G_CALLBACK (onCopyClicked), data);
   g_signal_connect (windowEditor, "close-request", G_CALLBACK (callback), windowEditor);

   gtk_window_set_child (GTK_WINDOW (windowEditor), box);
   gtk_window_present (GTK_WINDOW (windowEditor));
   g_free (content);
   return true;
}


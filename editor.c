/*! Text editor
   compilation: gcc -c editor.c `pkg-config --cflags gtk4 gtksourceview-5`
*/
#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <stdbool.h>
#include <stdio.h>
#include "rtypes.h"

typedef void (*Callback) (void *);
GtkApplicationWindow *windowEditor;
extern long    getFileSize (const char *fileName);
extern char   *formatThousandSep (char *buffer, size_t maxLen, int value);

/*! type to exchange data linked to editor */
typedef struct {
   GtkWidget *win;
   GtkSourceBuffer *buffer;
   GtkWidget *searchEntry;
   gint currentMatch;
   GtkWidget *textView;  // Ajout pour éviter la recherche
   GList *matches;
   char *fileName;
   char *str;
} EditorData;

/*! Copy button for editor */
static void onMyCopyClicked (GtkButton *button, gpointer user_data) {
   GtkNotebook *notebook = GTK_NOTEBOOK (user_data);

   // get active noteboook
   int page = gtk_notebook_get_current_page (notebook);
   if (page == -1) {
      fprintf (stderr, "In onMyCopyClicked, No active tab to copy from.\n");
      return;
   }

   // get GtkSourceView content in active notebook
   GtkWidget *scroll = gtk_notebook_get_nth_page (notebook, page);
   if (!scroll) {
      fprintf (stderr, "In onMyCopyClicked, Failed to retrieve current tab content.\n");
      return;
   }

   GtkWidget *sourceView = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW(scroll));
   if (!GTK_SOURCE_IS_VIEW (sourceView)) {
      fprintf (stderr, "In onMyCopyClicked, Current tab does not contain a source view.\n");
      return;
   }

   GtkSourceBuffer *sourceBuffer = GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (sourceView)));
   GtkTextIter start, end;
   gchar *text;

   gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER(sourceBuffer), &start, &end);
   text = gtk_text_buffer_get_text (GTK_TEXT_BUFFER(sourceBuffer), &start, &end, FALSE);

   // Copy text in clipboard
   GdkDisplay *display = gdk_display_get_default();
   GdkClipboard *clipboard = gdk_display_get_clipboard(display);
   gdk_clipboard_set_text (clipboard, text);

   printf ("In onMyCopyClicked: Text copied to clipboard.\n");
   g_free (text);
}

/*! select all string matching text  */
static void searchAndHighlight (GtkSourceBuffer *buffer, const gchar *text, GList **matches) {
   GtkTextIter startIter, endIter;
   gchar *content;

   // Nettoyer les précédentes correspondances
   *matches = NULL;

   // Récupérer la table des tags
   GtkTextTagTable *tagTable = gtk_text_buffer_get_tag_table(GTK_TEXT_BUFFER(buffer));

   // Chercher si le tag "highlightYellow" existe déjà
   GtkTextTag *highlightTag = gtk_text_tag_table_lookup (tagTable, "highlightYellow");

   // Si le tag n'existe pas, on le crée
   if (highlightTag == NULL) {
      highlightTag = gtk_text_buffer_create_tag (GTK_TEXT_BUFFER(buffer), "highlightYellow", "background", "yellow", NULL);
   }

   if (highlightTag == NULL) {
      fprintf(stderr, "In searchAndHighlight, Impossible to get tag for highlightYellow");
      return;
   }

   // Définir les itérateurs aux borne s du buffer
   gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER(buffer), &startIter, &endIter);

   // Supprimer les tags de surbrillance précédents si le tag existe
   if (GTK_IS_TEXT_TAG (highlightTag)) {
      gtk_text_buffer_remove_tag (GTK_TEXT_BUFFER(buffer), highlightTag, &startIter, &endIter);
   }

   // Obtenir le contenu du texte
   content = gtk_text_buffer_get_text (GTK_TEXT_BUFFER(buffer), &startIter, &endIter, FALSE);

   // Convertir les deux chaînes (texte et contenu) en minuscules pour rendre la recherche insensible à la casse
   gchar *lowercaseSearchText = g_utf8_strdown(text, -1);
   gchar *lowercaseContent = g_utf8_strdown(content, -1);
   GtkTextIter matchStartIter, matchEndIter;

   // Trouver toutes les correspondances du texte (insensible à la casse)
   const gchar *current_pos = lowercaseContent;
   while ((current_pos = g_strstr_len(current_pos, -1, lowercaseSearchText)) != NULL) {

      // Calculer l'offset en caractères UTF-8
      gint start_offset = g_utf8_pointer_to_offset (lowercaseContent, current_pos);
      gint end_offset = start_offset + g_utf8_strlen (lowercaseSearchText, -1);

      // Obtenir les itérateurs aux positions calculées
      gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER(buffer), &matchStartIter, start_offset);
      gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER(buffer), &matchEndIter, end_offset);

      // Ajouter la correspondance à la liste
      *matches = g_list_prepend (*matches, g_strdup (current_pos));

      // Appliquer le tag de surbrillance
      if (GTK_IS_TEXT_TAG(highlightTag)) {
         gtk_text_buffer_apply_tag (GTK_TEXT_BUFFER(buffer), highlightTag, &matchStartIter, &matchEndIter);
      }

      // Continuer à chercher à partir de la fin de la correspondance trouvée
      current_pos = g_utf8_next_char (current_pos);
   }

   // Libérer la mémoire allouée pour les chaînes minuscules
   g_free (lowercaseSearchText);
   g_free (lowercaseContent);
   g_free (content);
}

/*! Manage the search for editor in the current tab */
static void onMySearchClicked (GtkButton *button, gpointer user_data) {
   GtkNotebook *notebook = GTK_NOTEBOOK (user_data);

   // Identify active notebook
   int currentPage = gtk_notebook_get_current_page (notebook);
   if (currentPage < 0) {
      fprintf (stderr, "In onMySearchClicked, No active tab found.\n");
      return;
   }

   // get active notebook content
   GtkWidget *scroll = gtk_notebook_get_nth_page (notebook, currentPage);
   if (!scroll) {
      fprintf (stderr, "In onMySearchClicked, No scrollable container found in the current tab.\n");
      return;
   }

   // get data associated to notebook
   GtkWidget *sourceView = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (scroll));

   // check widget is a GtkSourceView instance
   if (!GTK_SOURCE_VIEW (sourceView)) {
     fprintf (stderr, "In onMySearchClicked, The widget in the current tab is not a source view.\n");
     return;
   }

   // get data associated to source view
   EditorData *data = g_object_get_data (G_OBJECT (sourceView), "editor-data");
   if (!data) {
      fprintf (stderr, "In onMySearchClicked, No editor data found for the current tab.\n");
      return;
   }

   // get text of research
   const gchar *searchText = gtk_editable_get_text (GTK_EDITABLE (data->searchEntry));
   if (!searchText || strlen (searchText) == 0) {
      fprintf (stderr, "In onMySearchClicked, Search text is empty.\n");
      return;
   }

   // Reinit association and search state
   data->currentMatch = 0;
   g_list_free (data->matches);
   data->matches = NULL;

   // print matches
   searchAndHighlight (data->buffer, searchText, &data->matches);
}

/*! Save the file with modifications */
static void onMySaveClicked (GtkButton *button, gpointer user_data) {
   GtkNotebook *notebook = GTK_NOTEBOOK(user_data);

   // get active notebook content
   int page = gtk_notebook_get_current_page (notebook);
   if (page == -1) {
      fprintf (stderr, "In onMySaveClicked, No active tab to save.\n");
      return;
   }

   // Get GtkSourceView contect in active notebook
   GtkWidget *scroll = gtk_notebook_get_nth_page (notebook, page);
   if (!scroll) {
      fprintf (stderr, "In onMySaveClicked, Failed to retrieve current tab content.\n");
      return;
   }

   GtkWidget *sourceView = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW(scroll));
   if (!GTK_SOURCE_IS_VIEW(sourceView)) {
      fprintf (stderr, "In onMySaveClicked, Current tab does not contain a source view.\n");
      return;
   }

   GtkSourceBuffer *sourceBuffer = GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW(sourceView)));
   GtkTextIter start, end;
   gchar *text;

   gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (sourceBuffer), &start, &end);
   text = gtk_text_buffer_get_text (GTK_TEXT_BUFFER(sourceBuffer), &start, &end, FALSE);

   // Get filename in notebbook tab
   GtkWidget *tabLabel = gtk_notebook_get_tab_label (GTK_NOTEBOOK(notebook), scroll);
   const char *fileName = gtk_label_get_text(GTK_LABEL (tabLabel));

   // save file 
   FILE *file = fopen (fileName, "w");
   if (file) {
      fputs (text, file);
      fclose (file);
      printf ("File saved successfully: %s\n", fileName);
   } else {
      fprintf (stderr, "In onMySaveClicked, Impossible to save file: %s\n", fileName);
   }

   g_free (text);
}

/*! color first line in red bold */
static void applySyntaxHighlight (GtkSourceBuffer *sourceBuffer) {
   GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(GTK_TEXT_BUFFER(sourceBuffer));
   GtkTextTag *firstLineTag = gtk_text_tag_table_lookup(tag_table, "firstLine");
   
   if (! firstLineTag) {
      firstLineTag = gtk_text_buffer_create_tag (GTK_TEXT_BUFFER (sourceBuffer),
                                                 "firstLine",  // Name
                                                 "foreground", "red",  // in red
                                                 "weight", PANGO_WEIGHT_BOLD,  // in bold
                                                 NULL);
   }
   GtkTextIter start, end;
   gtk_text_buffer_get_iter_at_line (GTK_TEXT_BUFFER (sourceBuffer), &start, 0);  // first line begin
   gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (sourceBuffer), &end, 0, 
                                           gtk_text_iter_get_chars_in_line(&start)); // first line end

   gtk_text_buffer_apply_tag (GTK_TEXT_BUFFER (sourceBuffer), firstLineTag, &start, &end);
}


/*! Comments in green */
static void applyCommentHighlighting (GtkSourceBuffer *sourceBuffer) {
   GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(GTK_TEXT_BUFFER (sourceBuffer));
   GtkTextTag *commentTag = gtk_text_tag_table_lookup (tag_table, "comment");

   if (!commentTag) {
      commentTag = gtk_text_buffer_create_tag (GTK_TEXT_BUFFER(sourceBuffer), 
                                               "comment",
                                               "foreground", "green", 
                                               "style", PANGO_STYLE_ITALIC, 
                                               NULL);
   }
   GtkTextIter start, end;

   // delete old tagd
   gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (sourceBuffer), &start);
   gtk_text_buffer_get_end_iter (GTK_TEXT_BUFFER (sourceBuffer), &end);
   gtk_text_buffer_remove_tag_by_name (GTK_TEXT_BUFFER (sourceBuffer), "comment", &start, &end);

   // apply tag
   gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (sourceBuffer), &start);
   while (gtk_text_iter_forward_search (&start, "#", GTK_TEXT_SEARCH_VISIBLE_ONLY, &start, &end, NULL)) {
      GtkTextIter line_end = start;
      gtk_text_iter_forward_to_line_end (&line_end);
      gtk_text_buffer_apply_tag (GTK_TEXT_BUFFER (sourceBuffer), commentTag, &start, &line_end);
      gtk_text_iter_forward_line (&start);
   }
}

/*! Manage change in buffer useful for comments syntax  coloration */
static void onBufferChanged (GtkTextBuffer *buffer, gpointer user_data) {
   GtkNotebook *notebook = GTK_NOTEBOOK (user_data);

   // get active noteboook
   int page = gtk_notebook_get_current_page (notebook);
   if (page == -1) {
      fprintf (stderr, "In onMyCopyClicked, No active tab to copy from.\n");
      return;
   }

   // get GtkSourceView content in active notebook
   GtkWidget *scroll = gtk_notebook_get_nth_page (notebook, page);
   if (!scroll) {
      fprintf (stderr, "In onMyCopyClicked, Failed to retrieve current tab content.\n");
      return;
   }

   GtkWidget *sourceView = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW(scroll));
   if (!GTK_SOURCE_IS_VIEW (sourceView)) {
      fprintf (stderr, "In onMyCopyClicked, Current tab does not contain a source view.\n");
      return;
   }

   GtkSourceBuffer *sourceBuffer = GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (sourceView)));
   applySyntaxHighlight (GTK_SOURCE_BUFFER (sourceBuffer));
   applyCommentHighlighting (GTK_SOURCE_BUFFER (sourceBuffer));
}

/*! Simple editor with tabs, finder, copy, and save buttons */
bool myEditor (GtkApplication *app, const char **fileNames, int fileCount, const char *title, Callback callback) {
   char str [MAX_SIZE_LINE];
   if (fileCount > 3) {
      fprintf (stderr, "In editor, Maximum of 3 files supported.\n");
      return false;
   }

   // Main window creation
   windowEditor = GTK_APPLICATION_WINDOW (gtk_application_window_new (app));
   gtk_window_set_default_size (GTK_WINDOW (windowEditor), 800, 600);
   gtk_window_set_title (GTK_WINDOW (windowEditor), title);

   // Notebook (tab container) creation
   GtkWidget *notebook = gtk_notebook_new ();

   // Tool bar creation
   GtkWidget *toolbar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
   GtkWidget *saveButton = gtk_button_new_with_label ("Save");
   gtk_box_append (GTK_BOX (toolbar), saveButton);
   GtkWidget *searchEntry = gtk_entry_new ();
   gtk_box_append (GTK_BOX (toolbar), searchEntry);

   GtkWidget *searchClicButton = gtk_button_new_from_icon_name ("edit-find");
   gtk_widget_set_tooltip_text (searchClicButton, "Find");
   GtkWidget *pasteButton = gtk_button_new_from_icon_name ("edit-copy");
   gtk_widget_set_tooltip_text (pasteButton, "Copy the content");
   gtk_box_append (GTK_BOX (toolbar), searchClicButton);
   gtk_box_append (GTK_BOX (toolbar), pasteButton);

   for (int i = 0; i < fileCount; i++) {
      const char *fileName = fileNames[i];
      FILE *file = fopen (fileName, "r");
      if (file == NULL) {
         fprintf (stderr, "In editor, Impossible to open: %s\n", fileName);
         return false;
      }
      // Create a tab label with the file name and size
      formatThousandSep (str, sizeof (str), getFileSize (fileName));
      char *title = g_strdup_printf ("%s, %s Bytes", fileName, str);
      GtkWidget *tabLabel = gtk_label_new (title);
      g_free (title);

      // Read content of the file
      fseek (file, 0, SEEK_END);
      size_t fileSize = ftell (file);
      rewind(file);

      char *content = g_malloc (fileSize + 1);
      size_t readSize = fread (content, 1, fileSize, file);
      fclose (file);

      if (readSize != fileSize) {
         fprintf (stderr, "In editor, Error reading file: %s\n", fileName);
         g_free (content);
         continue;
      }

      content [fileSize] = '\0';

      // GtkSourceView and buffer creation
      GtkWidget *sourceView = gtk_source_view_new ();
      GtkSourceBuffer *sourceBuffer = gtk_source_buffer_new (NULL);
      gtk_text_buffer_set_text (GTK_TEXT_BUFFER (sourceBuffer), content, -1);
      gtk_text_view_set_buffer (GTK_TEXT_VIEW (sourceView), GTK_TEXT_BUFFER (sourceBuffer));
      gtk_text_view_set_monospace (GTK_TEXT_VIEW (sourceView), TRUE);
      gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (sourceView), TRUE);

      // Data creation
      EditorData *data = g_new0 (EditorData, 1);
      data->buffer = sourceBuffer;
      data->textView = sourceView;  // 🔹 Stocke le GtkTextView ici
      data->fileName = g_strdup (fileName);
      data->searchEntry = searchEntry;
      data->matches = NULL;
      data->currentMatch = 0;

      g_object_set_data (G_OBJECT(sourceView), "editor-data", data);

      // Apply syntax highlighting (if applicable)
      applySyntaxHighlight (GTK_SOURCE_BUFFER (sourceBuffer));       // first line in red
      applyCommentHighlighting (GTK_SOURCE_BUFFER (sourceBuffer));   // comments in green

      // Scroll bar creation
      GtkWidget *scroll = gtk_scrolled_window_new ();
      gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), sourceView);
      gtk_widget_set_hexpand (scroll, TRUE);
      gtk_widget_set_vexpand (scroll, TRUE);

      // Add the scrollable source view to the notebook as a new tab
      gtk_notebook_append_page (GTK_NOTEBOOK (notebook), scroll, tabLabel);

      // Connect toolbar signals to the current buffer
      g_signal_connect (saveButton, "clicked", G_CALLBACK (onMySaveClicked), notebook);
      g_signal_connect (searchClicButton, "clicked", G_CALLBACK (onMySearchClicked), notebook);
      g_signal_handlers_disconnect_by_func (pasteButton, G_CALLBACK (onMyCopyClicked), notebook);
      g_signal_connect (pasteButton, "clicked", G_CALLBACK (onMyCopyClicked), notebook);

      if (callback != NULL)
         g_signal_connect (windowEditor, "close-request", G_CALLBACK (callback), windowEditor);
   
      g_signal_connect (sourceBuffer, "changed", G_CALLBACK (onBufferChanged), notebook);

      g_free(content);
   }

   // Main container with toolbar and notebook
   GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   gtk_box_append (GTK_BOX(box), toolbar);
   gtk_box_append (GTK_BOX(box), notebook);

   // Add the main container to the window
   gtk_window_set_child (GTK_WINDOW(windowEditor), box);
   gtk_window_present (GTK_WINDOW(windowEditor));

   return true;
}


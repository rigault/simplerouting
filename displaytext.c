/*! Display Text
   compilation: gcc -c displaytext.c `pkg-config --cflags gtk4`
*/
#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdio.h>
#include "rtypes.h"

#define  MAX_DISPLAY_WIDTH       1800
#define  MAX_DISPLAY_HEIGHT      800

struct {
   char firstLine [MAX_SIZE_LINE];                 // first line for displayText
   char *gloBuffer;                                // for filtering in displayText ()
} dispTextDesc;

/*! free DisplayText Resources */
void freeDisplayTextResources () {
   free (dispTextDesc.gloBuffer);
}

/*! Filter function for displayText. Return buffer with filtered text and number of lines (-1 if problem) */
static int filterText (GtkTextBuffer *buffer, const char *filter) {
   GtkTextIter start, end;
   char **lines = NULL;
   GRegex *regex = NULL;
   GError *error = NULL;
   int count = 0;

   // Compile the regular expression if a filter is provided
   if (filter != NULL) {
      regex = g_regex_new (filter, G_REGEX_CASELESS, 0, &error);
      if (error != NULL) {
         fprintf (stderr, "In filterText: Error compiling regex: %s\n", error->message);
         g_clear_error (&error);
         return -1;
      }
   }

   // Clear the buffer
   gtk_text_buffer_get_start_iter (buffer, &start);
   gtk_text_buffer_get_end_iter (buffer, &end);
   gtk_text_buffer_set_text (buffer, "", -1);

   // Split the input text
   lines = g_strsplit (dispTextDesc.gloBuffer, "\n", -1);
   if (lines == NULL) {
      fprintf (stderr, "In filterText: g_strsplit failed\n");
      if (regex != NULL) {
         g_regex_unref (regex);
      }
      return -1;
   }

   // Process each line
   for (int i = 0; lines [i] != NULL; i++) {
      if ((lines [i] != NULL) && (lines [i][0] != '\0') && ((filter == NULL) || g_regex_match (regex, lines[i], 0, NULL))) {
         gtk_text_buffer_insert_at_cursor (buffer, lines[i], -1);
         gtk_text_buffer_insert_at_cursor (buffer, "\n", -1);
         count++;
      }
   }

   // Free resources
   if (regex != NULL) {
      g_regex_unref (regex);
   }
   g_strfreev (lines);

   return count;
}

/*! for displayText filtering */
static void onFilterEntryChanged (GtkEditable *editable, gpointer userData) {
   GtkTextBuffer *buffer = (GtkTextBuffer *) userData;
   if (editable == NULL)
      filterText (buffer, NULL);
   else {
      const char *filter = gtk_editable_get_text (editable);
      filterText (buffer, filter);
   }
}

/*! for first line decorated */
static GtkWidget *createDecoratedLabel (const char *text) {
   GtkWidget *label = gtk_label_new (NULL);
   char *markup = g_markup_printf_escaped ("<span foreground='red' font_family='monospace'><b>%s</b></span>", text);

   gtk_label_set_markup (GTK_LABEL(label), markup);
   gtk_label_set_xalign (GTK_LABEL(label), 0.0);     // Align to the left
   gtk_widget_set_halign (label, GTK_ALIGN_FILL);    // Ensure the label fills the available horizontal space
   gtk_widget_set_hexpand (label, TRUE);             // Allow the label to expand horizontally
   g_free (markup);
   return label;
}

/* first line of text and return pos of second line */
static char *extractFirstLine (const char *text, char* strFirstLine, size_t maxLen) {
   const char *firstLineEnd = strchr (text, '\n');

   if (firstLineEnd == NULL) {             // no newline. First line is empty
      strFirstLine [0] = '\0';             // Null-terminate the string
      return NULL;
   }
   size_t length = (size_t) (firstLineEnd - text);         // length of first line
   if (length >= maxLen) length = maxLen - 1;              // make sure no overflow
   g_strlcpy (strFirstLine, text, length + 1);
   return (char *) (firstLineEnd + 1);
}

/*! Callback semicolon replace  */
static void onCheckBoxSemiColonToggled (GtkWidget *checkbox, gpointer userData) {
   GtkTextBuffer *buffer = (GtkTextBuffer *) userData;
   char *strBuffer = g_strdup (dispTextDesc.gloBuffer);

   if (! gtk_check_button_get_active ((GtkCheckButton *) checkbox))
      g_strdelimit (strBuffer, ";", ' ');
   gtk_text_buffer_set_text (buffer, strBuffer, -1);
   g_free (strBuffer);
}

/*! paste button for displayText */
static void onPasteButtonClicked () {
   char *strClipBoard = g_strdup_printf ("%s\n%s", dispTextDesc.firstLine, dispTextDesc.gloBuffer);

   GdkDisplay *display = gdk_display_get_default ();
   GdkClipboard *clipboard = gdk_display_get_clipboard (display);
   gdk_clipboard_set_text (clipboard, strClipBoard);
   g_free (strClipBoard);
}

/*! display text with monospace police and filtering function using regular expression 
   first line is decorated */
void displayText (GtkApplication *app, const char *text, size_t maxLen, const char *title, const char *statusStr) {

   char *ptSecondLine = extractFirstLine (text, dispTextDesc.firstLine, sizeof (dispTextDesc.firstLine));
   if (ptSecondLine == NULL) {
      fprintf (stderr, "In displayText: no first line\n");
      return;
   }
   char *tempBuffer = realloc (dispTextDesc.gloBuffer, maxLen);
   if (tempBuffer == NULL) {
    fprintf (stderr, "In displayText: realloc failed\n");
    return;
   }
   dispTextDesc.gloBuffer = tempBuffer;

   g_strlcpy (dispTextDesc.gloBuffer, ptSecondLine, maxLen);
   
   GtkWidget *textWindow = gtk_application_window_new (app);
   gtk_window_set_title (GTK_WINDOW (textWindow), title);
   //gtk_window_set_default_size (GTK_WINDOW (textWindow), width, height);

   // g_signal_connect (window, "destroy", G_CALLBACK(onParentDestroy), textWindow);
   
   GtkWidget *vBox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
   gtk_window_set_child (GTK_WINDOW (textWindow), vBox);

   GtkWidget *hBox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
   GtkWidget *filterLabel = gtk_label_new ("Filter: ");
   GtkEntryBuffer *filterBuffer = gtk_entry_buffer_new ("", -1);
   GtkWidget *filterEntry = gtk_entry_new_with_buffer (filterBuffer);

   // button copy
   GtkWidget *copyButton = gtk_button_new_from_icon_name ("edit-copy");
   gtk_widget_set_tooltip_text (copyButton, "Copy Content");
   g_signal_connect (copyButton, "clicked", G_CALLBACK (onPasteButtonClicked), NULL);
   
   GtkWidget *firstLine = createDecoratedLabel (dispTextDesc.firstLine);

   GtkWidget *scrolled_window = gtk_scrolled_window_new ();
   gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_widget_set_hexpand (scrolled_window, TRUE);
   gtk_widget_set_vexpand (scrolled_window, TRUE);
   
   GtkWidget *textView = gtk_text_view_new();
   gtk_text_view_set_monospace (GTK_TEXT_VIEW(textView), TRUE);
   
   // read only text
   gtk_text_view_set_editable (GTK_TEXT_VIEW(textView), FALSE);
   gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (textView), FALSE);
   gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (textView), GTK_WRAP_WORD_CHAR);
   gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), textView);

   GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW(textView));
   // gtk_text_buffer_insert_at_cursor (buffer, ptSecondLine, -1);
   gtk_text_buffer_set_text (buffer, ptSecondLine, -1);

   //checkbox: "replace ;"
   GtkWidget *checkboxSemiColonRep = gtk_check_button_new_with_label ("SemiColon visible");
   gtk_check_button_set_active ((GtkCheckButton *) checkboxSemiColonRep, 1);
   
   g_signal_connect (G_OBJECT(checkboxSemiColonRep), "toggled", G_CALLBACK (onCheckBoxSemiColonToggled), buffer);

   // status bar at the bottom
   GtkWidget *statusbarText = gtk_label_new (statusStr);
   // gtk_label_set_xalign(GTK_LABEL (statusbarText), 0);      // left justified
   
   gtk_box_append (GTK_BOX (hBox), filterLabel);
   gtk_box_append (GTK_BOX (hBox), filterEntry);
   gtk_box_append (GTK_BOX (hBox), checkboxSemiColonRep);
   gtk_box_append (GTK_BOX (hBox), copyButton);

   gtk_box_append (GTK_BOX (vBox), hBox);
   gtk_box_append (GTK_BOX (vBox), firstLine);
   gtk_box_append (GTK_BOX (vBox), scrolled_window);
   gtk_box_append (GTK_BOX (vBox), statusbarText);

   onFilterEntryChanged (NULL, buffer);   
   g_signal_connect (filterEntry, "changed", G_CALLBACK (onFilterEntryChanged), buffer);

   // Get size requested for text
   PangoContext *pango_context = gtk_widget_get_pango_context(textView);
   PangoLayout *layout = pango_layout_new(pango_context);
   pango_layout_set_text(layout, ptSecondLine, -1);

   int textWidth, textHeight;
   pango_layout_get_pixel_size (layout, &textWidth, &textHeight);
   g_object_unref(layout);

   // Adjust window size 
   guint finalWidth = MIN (textWidth + 50, MAX_DISPLAY_WIDTH);
   guint finalHeight = MIN (textHeight + 100, MAX_DISPLAY_HEIGHT); 

   gtk_window_set_default_size (GTK_WINDOW(textWindow), finalWidth, finalHeight);

   gtk_window_present (GTK_WINDOW (textWindow));
}


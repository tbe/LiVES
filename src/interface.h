// interface.h
// LiVES
// (c) G. Finch 2003 - 2012 <salsaman@xs4all.nl,salsaman@gmail.com>
// Released under the GNU GPL 3 or later
// see file ../COPYING for licensing details

#ifndef HAS_LIVES_INTERFACE_H
#define HAS_LIVES_INTERFACE_H

// a few GUI specific settings
#define DEFAULT_FRAME_HSIZE 320
#define DEFAULT_FRAME_VSIZE 200

#define PROCW_STRETCHER "                                                                                                                            " ///< blank label to show so our message dialogs are not too small

#define ENC_DETAILS_WIN_H 200 ///< horizontal size in pixels of the encoder output window
#define ENC_DETAILS_WIN_V 200 ///< vertical size in pixels of the encoder output window

#define MAX_MSG_WIDTH_CHARS 100 ///< max width of text on warning/error labels

void load_theme (void);

GtkWidget* create_window4 (void);
GtkWidget* create_dialog2 (gint warning_mask);
GtkWidget* create_dialog3 (const gchar *text, boolean is_blocking, gint warning_mask);
GtkWidget* create_opensel_dialog (void);
GtkWidget* create_encoder_prep_dialog (const gchar *text1, const gchar *text2, boolean opt_resize);

void widget_add_preview(GtkWidget *widget, LiVESBox *for_preview, LiVESBox *for_button, 
			LiVESBox *for_deinterlace, int preview_type);  ///< for fileselector preview

boolean do_audio_choice_dialog(short startup_phase);

void do_layout_recover_dialog(void);

GtkWidget *create_cleardisk_advanced_dialog(void);

typedef struct _fileinfo {
  GtkWidget *info_window;
  GtkWidget *textview24;
  GtkWidget *textview25;
  GtkWidget *textview26;
  GtkWidget *textview27;
  GtkWidget *textview28;
  GtkWidget *textview29;
  GtkWidget *textview_ltime;
  GtkWidget *textview_rtime;
  GtkWidget *textview_lrate;
  GtkWidget *textview_rrate;
} fileinfo;

fileinfo* create_info_window (gint audio_channels, boolean is_mt);


typedef struct {
  GtkWidget *dialog;
  GtkWidget *entry;
  GtkWidget *dir_entry;
  GtkWidget *name_entry;
  GtkWidget *warn_checkbutton;
  GList *setlist;
} _entryw;

_entryw* create_rename_dialog (gint type);
_entryw* create_location_dialog (gint type);
_entryw* create_cds_dialog (gint type);

typedef struct __insertw {
  GtkWidget *insert_dialog;
  GtkWidget *with_sound;
  GtkWidget *without_sound;
  GtkWidget *spinbutton_times;
  GtkWidget *fit_checkbutton;
} _insertw;

_insertw* create_insert_dialog (void);


typedef struct __commentsw {
  GtkWidget *comments_dialog;
  GtkWidget *title_entry;
  GtkWidget *author_entry;
  GtkWidget *comment_entry;
  GtkWidget *subt_checkbutton;
  GtkWidget *subt_entry;
} _commentsw;

_commentsw* create_comments_dialog (file *sfile, gchar *filename);


typedef struct {
  GtkWidget *dialog;
  GtkWidget *clear_button;
  GtkWidget *delete_button;
  GtkWidget *textview;
} text_window;

text_window* create_text_window (const gchar *title_part, const gchar *text, GtkTextBuffer *);


typedef struct {
  GtkWidget *dialog;
  GtkWidget *time_spin;
  boolean is_sel;
} aud_dialog_t;


typedef struct {
  boolean use_advanced;
  GtkWidget *advbutton;
  GtkWidget *adv_vbox;
  GtkWidget *combod;
  GtkWidget *comboo;
  GtkWidget *spinbuttoni;
  GtkWidget *spinbuttonw;
  GtkWidget *spinbuttonh;
  GtkWidget *spinbuttonf;
  GtkWidget *radiobuttond;

} lives_tvcardw_t;



aud_dialog_t *create_audfade_dialog (gint type);
GtkWidget *create_combo_dialog (gint type, gpointer user_data);

_commentsw *commentsw;
_entryw *renamew;
_entryw *locw;
_insertw *insertw;
text_window *textwindow;


#define MAX_FADE_SECS 30. ///< max time to offer fade audio in/out for



#endif

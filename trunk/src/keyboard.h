// keyboard.h
// LiVES
// (c) G. Finch 2004 - 2009 <salsaman@xs4all.nl>
// see file ../COPYING for licensing details

// repeating keys
guint16 cached_key;
guint16 cached_mod;

// these keys should be cached on a key down and sent every time until a key up
#define key_left 100
#define key_left2 113
#define key_right 102
#define key_right2 114
#define key_up 98
#define key_up2 111
#define key_down 104
#define key_down2 116


// smooth key repeat for some keys
gboolean key_snooper (GtkWidget *widget, GdkEventKey *event, gpointer data);

gboolean 
plugin_poll_keyboard (gpointer data);

gboolean 
pl_key_function (gboolean down, guint16 unicode, guint16 keymod);

gboolean faster_callback (GtkAccelGroup *, GObject *, guint, GdkModifierType, gpointer user_data);

gboolean slower_callback (GtkAccelGroup *, GObject *, guint, GdkModifierType, gpointer user_data);

gboolean skip_back_callback (GtkAccelGroup *, GObject *, guint, GdkModifierType, gpointer user_data);

gboolean skip_forward_callback (GtkAccelGroup *, GObject *, guint, GdkModifierType, gpointer user_data);

gboolean stop_callback (GtkAccelGroup *, GObject *, guint, GdkModifierType, gpointer user_data);

gboolean rec_callback (GtkAccelGroup *, GObject *, guint, GdkModifierType, gpointer user_data);

gboolean loop_callback (GtkAccelGroup *, GObject *, guint, GdkModifierType, gpointer user_data);

gboolean loop_cont_callback (GtkAccelGroup *, GObject *, guint, GdkModifierType, gpointer user_data);

gboolean ping_pong_callback (GtkAccelGroup *, GObject *, guint, GdkModifierType, gpointer user_data);

gboolean dblsize_callback (GtkAccelGroup *, GObject *, guint, GdkModifierType, gpointer user_data);

gboolean showfct_callback (GtkAccelGroup *, GObject *, guint, GdkModifierType, gpointer user_data);

gboolean fullscreen_callback (GtkAccelGroup *, GObject *, guint, GdkModifierType, gpointer user_data);

gboolean sepwin_callback (GtkAccelGroup *, GObject *, guint, GdkModifierType, gpointer user_data);

gboolean fade_callback (GtkAccelGroup *, GObject *, guint, GdkModifierType, gpointer user_data);


// key rpt (millisecs) for kb rpt, should be << FPS_MAX
#define KEY_RPT_INTERVAL 40


/* default MIDI checks per keyboard cycle (i.e. normally x checks per 40 ms) */
/* this should be just over 1000, but some keyboards send spurious sysex, which slows down the playback */
/* can be over-ridden anyway in prefs */

#define DEF_MIDI_CHECK_RATE 10


/* allowed non-reads between reads */
#define DEF_MIDI_RPT 1000

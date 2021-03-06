// support.h
// LiVES
// portions of this file were auto-generated by glade, the remainder is (c) G. Finch (salsaman) 2002 - 2015

// released under the GNU GPL 3 or later
// see file ../COPYING or www.gnu.org for licensing details

#ifndef HAS_LIVES_SUPPORT_H
#define HAS_LIVES_SUPPORT_H

#ifndef NO_GTK
#include <gtk/gtk.h>
#define lives_strdup(a) g_strdup(a)
#define lives_free(a) g_free(a)
#define lives_locale_to_utf8(a,b,c,d,e) g_locale_to_utf8(a,b,c,d,e)
#endif

/*
 * Standard gettext macros.
 */
#ifdef ENABLE_NLS

char *trString;
char *translate(const char *String);
char *translate_with_plural(const char *String, const char *StringPlural, unsigned long int n);

#  include <libintl.h>
#  undef _
#  define _(String) (translate(String))
#  define P_(String,StringPlural,n) (translate_with_plural(String,StringPlural,n))
#  ifdef gettext_noop
#    define N_(String) gettext_noop (String)
#  else
#    define N_(String) (String)
#  endif
#else
#  define textdomain(String) (String)
#  define gettext(String) (String)
#  define dgettext(Domain,Message) (Message)
#  define dngettext(Domain,Message,MsgPlur,n) (Message)
#  define dcgettext(Domain,Message,Type) (Message)
#  define bindtextdomain(Domain,Directory) (Domain)
#  define _(String) (String)
#  define N_(String) (String)
#  define P_(String,StringPlural,n) (String)
#endif

#endif


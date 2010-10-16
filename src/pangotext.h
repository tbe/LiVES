// pangotext.h
// text handling code
// (c) A. Penkov 2010
// pieces of code taken and modified from scribbler.c
// released under the GNU GPL 3 or later
// see file COPYING or www.gnu.org for details

#ifndef LIVES_PANGOTEXT_H
#define LIVES_PANGOTEXT_H

typedef enum {
  SUBTITLE_TYPE_NONE=0,
  SUBTITLE_TYPE_SRT,
  SUBTITLE_TYPE_SUB
} lives_subtitle_type_t;


// for future use
typedef struct {
  lives_colRGB24_t fg;
  lives_colRGB24_t bg;
} lives_subtitle_style_t;


typedef struct lives_subtitle_t _lives_subtitle_t;

typedef struct {
  double start_time;
  double end_time;
  lives_subtitle_style_t *style; ///< for future use
  long textpos;
  _lives_subtitle_t *prev; ///< for future use
  _lives_subtitle_t *next;
} lives_subtitle_t;


typedef struct {
  lives_subtitle_type_t type;
  FILE *tfile;
  char *text;
  double last_time;
  lives_subtitle_t *index;
  lives_subtitle_t *current; ///< pointer to current entry in index
  int offset; ///< offset in frames (default 0)
} lives_subtitles_t;


typedef enum {
  LIVES_TEXT_MODE_FOREGROUND_ONLY,
  LIVES_TEXT_MODE_FOREGROUND_AND_BACKGROUND,
  LIVES_TEXT_MODE_BACKGROUND_ONLY
} lives_text_mode_t;


char **get_font_list(void);

gboolean render_text_to_layer(weed_plant_t *layer, const char *text, const char *fontname,\
  double size, lives_text_mode_t mode, lives_colRGBA32_t *fg_col, lives_colRGBA32_t *bg_col,\
  gboolean center, gboolean rising, double top);

#endif

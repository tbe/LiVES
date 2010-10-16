// audio.h
// LiVES (lives-exe)
// (c) G. Finch 2005 - 2009
// Released under the GPL 3 or later
// see file ../COPYING for licensing details

#ifndef _HAS_LIVES_AUDIO_H
#define _HAS_LIVES_AUDIO_H

#define SAMPLE_MAX_16BIT_P  32767.0f
#define SAMPLE_MAX_16BIT_N  32768.0f
#define SAMPLE_MAX_16BITI  32768

///sign swapping
#define SWAP_U_TO_S 1
#define SWAP_S_TO_U 2

///endian swapping
#define SWAP_X_TO_L 1
#define SWAP_L_TO_X 2


/// defaults for when not specifed
# define DEFAULT_AUDIO_RATE 44100
# define DEFAULT_AUDIO_CHANS 2
# define DEFAULT_AUDIO_SAMPS 16
# define DEFAULT_AUDIO_SIGNED8 (AFORM_UNSIGNED)
# define DEFAULT_AUDIO_SIGNED16 !(AFORM_UNSIGNED)


/// TODO ** - make configurable - audio buffer size for rendering
#define MAX_AUDIO_MEM 8*1024*1024

/// chunk size for interpolate/effect cycle
#define RENDER_BLOCK_SIZE 1024

/// size of silent block in bytes
#define SILENCE_BLOCK_SIZE 65536

/// chunk size for audio buffer reads
#define READ_BLOCK_SIZE 4096

/// buffer size for realtime audio
#define XSAMPLES 128000


/////////////////////////////////////
/// asynch msging


#define ASERVER_CMD_PROCESSED 0
#define ASERVER_CMD_FILE_OPEN 1
#define ASERVER_CMD_FILE_CLOSE 2
#define ASERVER_CMD_FILE_SEEK 3

/* message passing structure */
typedef struct _aserver_message_t {
  gint command;
  gchar *data;
  volatile struct _aserver_message_t *next;
} aserver_message_t;


typedef struct {
  guchar* data;
  size_t  size;
} audio_buffer_t;


//////////////////////////////////////////

typedef enum lives_audio_loop {
  AUDIO_LOOP_NONE,
  AUDIO_LOOP_FORWARD,
  AUDIO_LOOP_PINGPONG
} lives_audio_loop_t;



void sample_silence_dS (float *dst, unsigned long nsamples);

void sample_move_d8_d16(short *dst, guchar *src,
			unsigned long nsamples, size_t tbytes, float scale, int nDstChannels, int nSrcChannels, int swap_sign);

void sample_move_d16_d16(short *dst, short *src,
			 unsigned long nsamples, size_t tbytes, float scale, int nDstChannels, int nSrcChannels, int swap_endian, int swap_sign);

void sample_move_d16_d8(uint8_t *dst, short *src,
			unsigned long nsamples, size_t tbytes, float scale, int nDstChannels, int nSrcChannels, int swap_sign);

void sample_move_d16_float (float *dst, short *src, unsigned long nsamples, unsigned long src_skip, int is_unsigned, float vol);

long sample_move_float_int(void *holding_buff, float **float_buffer, int nsamps, float scale, int chans, int asamps, int usigned, gboolean swap_endian, float vol); ///< returns frames output

long sample_move_abuf_float (float **obuf, int nchans, int nsamps, int out_arate, float vol);

long sample_move_abuf_int16 (short *obuf, int nchans, int nsamps, int out_arate);

long render_audio_segment(gint nfiles, gint *from_files, gint to_file, gdouble *avels, gdouble *fromtime, weed_timecode_t tc_start, weed_timecode_t tc_end, gdouble *chvol, gdouble opvol_start, gdouble opvol_end, lives_audio_buf_t *obuf);

void aud_fade(gint fileno, gdouble startt, gdouble endt, gdouble startv, gdouble endv); ///< fade in/fade out


#define RECA_WINDOW_GRAB 1
#define RECA_NEW_CLIP 2
#define RECA_EXISTING 3


#ifdef ENABLE_JACK
void jack_rec_audio_to_clip(gint fileno, gint oldfileno, gshort rec_type);  ///< record from external source to clip
void jack_rec_audio_end(void);
#endif

#ifdef HAVE_PULSE_AUDIO
void pulse_rec_audio_to_clip(gint fileno, gint oldfileno, gshort rec_type);  ///< record from external source to clip
void pulse_rec_audio_end(void);
#endif

void fill_abuffer_from(lives_audio_buf_t *abuf, weed_plant_t *event_list, weed_plant_t *st_event, gboolean exact);


gboolean resync_audio(gint frameno);


lives_audio_track_state_t *get_audio_and_effects_state_at(weed_plant_t *event_list, weed_plant_t *st_event, gboolean get_audstate, gboolean exact);


void init_jack_audio_buffers (gint achans, gint arate, gboolean exact);
void free_jack_audio_buffers(void);

void init_pulse_audio_buffers (gint achans, gint arate, gboolean exact);
void free_pulse_audio_buffers(void);

void audio_free_fnames(void);


#endif
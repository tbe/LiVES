// LiVES - flv decoder plugin
// (c) G. Finch 2011 <salsaman@xs4all.nl>
// released under the GNU GPL 3 or later
// see file COPYING or www.gnu.org for details

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>

const char *plugin_version="LiVES flv decoder version 1.0";

#ifdef HAVE_AV_CONFIG_H
#undef HAVE_AV_CONFIG_H
#endif

#include <libavcodec/avcodec.h>

#include "decplugin.h"
#include "flv_decoder.h"

////////////////////////////////////////////////////////////////////////////

static int pix_fmt_to_palette(enum PixelFormat pix_fmt, int *clamped) {
 
    switch (pix_fmt) {
    case PIX_FMT_RGB24:
	return WEED_PALETTE_RGB24;
    case PIX_FMT_BGR24:
	return WEED_PALETTE_BGR24;
    case PIX_FMT_RGBA:
	return WEED_PALETTE_RGBA32;
    case PIX_FMT_BGRA:
	return WEED_PALETTE_BGRA32;
    case PIX_FMT_ARGB:
	return WEED_PALETTE_ARGB32;
    case PIX_FMT_YUV444P:
	return WEED_PALETTE_YUV444P;
    case PIX_FMT_YUV422P:
	return WEED_PALETTE_YUV422P;
    case PIX_FMT_YUV420P:
	return WEED_PALETTE_YUV420P;
    case PIX_FMT_YUYV422:
	return WEED_PALETTE_YUYV;
    case PIX_FMT_UYVY422:
	return WEED_PALETTE_UYVY;
    case PIX_FMT_UYYVYY411:
	return WEED_PALETTE_YUV411;
    case PIX_FMT_GRAY8:
    case PIX_FMT_Y400A:
	return WEED_PALETTE_A8;
    case PIX_FMT_MONOWHITE:
    case PIX_FMT_MONOBLACK:
	return WEED_PALETTE_A1;
    case PIX_FMT_YUVJ422P:
	if (clamped) *clamped=WEED_YUV_CLAMPING_UNCLAMPED;
	return WEED_PALETTE_YUV422P;
    case PIX_FMT_YUVJ444P:
	if (clamped) *clamped=WEED_YUV_CLAMPING_UNCLAMPED;
	return WEED_PALETTE_YUV444P;
    case PIX_FMT_YUVJ420P:
	if (clamped) *clamped=WEED_YUV_CLAMPING_UNCLAMPED;
	return WEED_PALETTE_YUV420P;

    default:
	return WEED_PALETTE_END;
    }
}



/* can enable this later to handle pix_fmt (35) - YUVA4204P */
/*
static void convert_quad_chroma(guchar *src, int width, int height, guchar *dest) {
  // width and height here are width and height of dest chroma planes, in bytes
  
  // double the chroma samples vertically and horizontally, with interpolation, eg. 420p to 444p

  // output to planes

  register int i,j;
  guchar *d_u=dest,*s_u=src;
  gboolean chroma=FALSE;
  int height2=height;
  int width2=width;

  height>>=1;
  width>>=1;

  // for this algorithm, we assume chroma samples are aligned like mpeg

  for (i=0;i<height2;i++) {
    d_u[0]=d_u[1]=s_u[0];
    for (j=2;j<width2;j+=2) {
      d_u[j+1]=d_u[j]=s_u[(j>>1)];
      d_u[j-1]=avg_chroma(d_u[j-1],d_u[j]);
      if (!chroma&&i>0) {
	// pass 2
	// average two src rows (e.g 2 with 1, 4 with 3, ... etc) for odd dst rows
	// thus dst row 1 becomes average of src chroma rows 0 and 1, etc.)
	d_u[j-width2]=avg_chroma(d_u[j-width2],d_u[j]);
	d_u[j-1-width2]=avg_chroma(d_u[j-1-width2],d_u[j-1]);
      }
    }
    if (!chroma&&i>0) {
      d_u[j-1-width2]=avg_chroma(d_u[j-1-width2],d_u[j-1]);
    }
    if (chroma) {
      s_u+=width;
    }
    chroma=!chroma;
    d_u+=width2;
  }
}

*/

//////////////////////////////////////////////


static int frame_to_dts(const lives_clip_data_t *cdata, int64_t frame) {
  return (int)((double)(frame)*1000./cdata->fps);
}

static int64_t dts_to_frame(const lives_clip_data_t *cdata, int dts) {
  return (int64_t)((double)dts/1000.*cdata->fps+.5);
}

static double getfloat64(unsigned char *data) {
  int64_t v=(((int64_t)(data[0]&0xFF)<<56)+((int64_t)(data[1]&0xFF)<<48)+((int64_t)(data[2]&0xFF)<<40)+((int64_t)(data[3]&0xFF)<<32)+((int64_t)(data[4]&0xFF)<<24)+((int64_t)(data[5]&0xFF)<<16)+((int64_t)(data[6]&0XFF)<<8)+(int64_t)(data[7]&0xFF));
  return ldexp(((v&((1LL<<52)-1)) + (1LL<<52)) * (v>>63|1), (v>>52&0x7FF)-1075);
}



//////////////////////////////////////////////////////////////////

static boolean lives_flv_parse_pack_header(const lives_clip_data_t *cdata, lives_flv_pack_t *pack) {
  lives_flv_priv_t *priv=cdata->priv;
  char data[FLV_PACK_HEADER_SIZE];

  lseek(priv->fd,priv->input_position,SEEK_SET);

  if (read (priv->fd, data, FLV_PACK_HEADER_SIZE) < FLV_PACK_HEADER_SIZE) {
    //fprintf(stderr, "flv_decoder: unable to read packet header for %s\n",cdata->URI);
    return FALSE;
  }

  priv->input_position+=FLV_PACK_HEADER_SIZE;

  pack->type=data[0];

  pack->size=((data[1]&0XFF)<<16)+((data[2]&0XFF)<<8)+(data[3]&0xFF);

  pack->dts=((data[7]&0xFF)<<24)+((data[4]&0xFF)<<16)+((data[5]&0xFF)<<8)+(data[6]&0xFF); // milliseconds


  //skip bytes 8,9,10 - stream id always 0

  //printf("pack size of %d\n",pack->size);

  return TRUE;
}


static int32_t get_last_tagsize(const lives_clip_data_t *cdata) {
  lives_flv_priv_t *priv=cdata->priv;
  unsigned char data[4];

  if (read (priv->fd, data, 4) < 4) return -1;
  return ((data[0]&0xFF)<<24)+((data[1]&0xFF)<<16)+((data[2]&0xFF)<<8)+(data[3]&0xFF);
}



static off_t get_last_packet_pos(const lives_clip_data_t *cdata) {
  lives_flv_priv_t *priv=cdata->priv;
  int32_t tagsize;
  off_t offs=lseek(priv->fd,-4,SEEK_END);

  tagsize=get_last_tagsize(cdata);

  return offs-tagsize;
}


static int get_last_video_dts(const lives_clip_data_t *cdata) {
  lives_flv_priv_t *priv=cdata->priv;
  lives_flv_pack_t pack;
  int delta,tagsize;
  unsigned char flags;
  off_t offs;

  priv->input_position=get_last_packet_pos(cdata);

  while (1) {
    if (!lives_flv_parse_pack_header(cdata,&pack)) return -1;
    delta=-15;
    if (pack.type==FLV_TAG_TYPE_VIDEO) {
      if (read (priv->fd, &flags, 1) < 1) return -1;
      if (!((flags & 0xF0)==0x50)) break;
      delta-=1;
    }
    // rewind to previous packet
    offs=lseek(priv->fd,delta,SEEK_CUR);
    if (offs<=0) return -1;
    tagsize=get_last_tagsize(cdata);
    priv->input_position-=15+tagsize;
  }
  return pack.dts;
}


static boolean is_keyframe(int fd) {
  unsigned char data[2];

  // read 6 bytes
  if (read (fd, data, 2) < 2) return FALSE;

  if ((data[0]&0xF0)==0x10) return TRUE;

  if ((data[0]&0xF0)==0x50 && data[1]==0) return TRUE; // TODO *** - h264 ? may need to seek to next video frame

  return FALSE;
}


/////////////////////////////////////////////////////

static void index_free(index_entry *idx) {
  index_entry *cidx=idx,*next;

  while (cidx!=NULL) {
    next=cidx->next;
    free(cidx);
    cidx=next;
  }
}



/// here we assume that pts of interframes > pts of previous keyframe
// should be true for most formats (except eg. dirac)

// we further assume that pts == dts for all frames


static index_entry *index_walk(index_entry *idx, int pts) {
  while(idx!=NULL) {
    if (pts>=idx->dts&&pts<=idx->dts_max) return idx;
    idx=idx->next;
  }
  /// oops. something went wrong
  return NULL; 
}


index_entry *index_upto(const lives_clip_data_t *cdata, int pts) {
  lives_flv_priv_t *priv=cdata->priv;
  lives_flv_pack_t pack;
  index_entry *nidx=priv->idxht,*oldht=nidx;
  int mid_dts=frame_to_dts(cdata,cdata->nframes-1)>>1;

  if (nidx==NULL) priv->input_position=priv->data_start;
  else priv->input_position=nidx->offs;

  while (1) {
    if (!lives_flv_parse_pack_header(cdata,&pack)) return NULL;

    // TODO *** - for non-avc, may need to find next non-control video packet


    if (pack.type==FLV_TAG_TYPE_VIDEO&&pack.size>0) {
      if (is_keyframe(priv->fd)) {

	if (pack.dts>mid_dts||(priv->idxth!=NULL&&pack.dts>=priv->idxth->dts)) {
	  // handle case where we cross the mid point, or head and tail list have met
	  if (priv->idxth!=NULL&&pack.dts>=priv->idxth->dts) {
	    // two lists are now contiguous; swap pointers to indicate this; update old head-tail and return it
	    
	    // we found no keyframes between idxht and idxth, so therefore extend idxht
	    priv->idxht->dts_max=priv->idxth->dts-1;
	    priv->idxht->next=priv->idxth;
	    nidx=priv->idxht; // this is the value we will return
	    
	    // cross the pointers
	    priv->idxht=index_walk(priv->idxht,mid_dts*4/3);
	    priv->idxth=index_walk(priv->idxhh,mid_dts*2/3);
	    
	    return nidx;
	  }
	  
	  priv->idxht->dts_max=pack.dts-1;
	  if (pack.dts>pts) return priv->idxht;

	  // we crossed the mid point but head list is too short
	  return index_downto(cdata,pts); // index from head of tail list down to pts
	}

	// add new keyframe
	nidx=(index_entry *)malloc(sizeof(index_entry));
	nidx->offs=priv->input_position-11;
	nidx->dts=nidx->dts_max=pack.dts;
	nidx->next=NULL;
	if (priv->idxht!=NULL) {
	  oldht=priv->idxht;
	  oldht->dts_max=pack.dts-1;
	  oldht->next=nidx;
	}
	else priv->idxhh=nidx;
    
	priv->idxht=nidx;
      }

      if (pack.dts==pts) {
	// result is current keyframe
	break;
      }
      if (pack.dts>pts) {
	// result is previous keyframe
	nidx=oldht;
	break;
      }

    }

    // get next packet
    priv->input_position+=pack.size+4;

  }

  return nidx;
}



index_entry *index_downto(const lives_clip_data_t *cdata, int pts) {
  int32_t tagsize;
  lives_flv_priv_t *priv=cdata->priv;
  lives_flv_pack_t pack;
  int delta;
  index_entry *nidx=priv->idxth;
  int mid_dts=frame_to_dts(cdata,cdata->nframes-1)>>1;

  if (nidx==NULL) priv->input_position=get_last_packet_pos(cdata);
  else {
    lseek(priv->fd,nidx->offs-4,SEEK_SET);
    tagsize=get_last_tagsize(cdata);
    priv->input_position=nidx->offs-4-tagsize;
  }

  while (1) {
    if (!lives_flv_parse_pack_header(cdata,&pack)) return NULL;

    if (pack.size==0||pack.type!=FLV_TAG_TYPE_VIDEO||(pack.type==FLV_TAG_TYPE_VIDEO&&!is_keyframe(priv->fd))) {
      // get previous packet
      delta=-15;
      if (pack.type==FLV_TAG_TYPE_VIDEO) delta-=2; // keyframe check uses 2 bytes
      lseek(priv->fd,delta,SEEK_CUR);
      tagsize=get_last_tagsize(cdata);
      priv->input_position-=15+tagsize;
      continue;
    }


    if (pack.dts<=mid_dts||(priv->idxht!=NULL&&pack.dts<=priv->idxht->dts_max)) {
      // handle case where we cross the mid point, or head and tail list have met
      if (priv->idxht!=NULL&&pack.dts<=priv->idxht->dts_max) {
	// two lists are now contiguous; swap pointers to indicate this; update old head-tail and return it

	// we found no keyframes between idxth (or last_dts) and idxht, so therefore extend idxht
	if (priv->idxth!=NULL) {
	  priv->idxht->dts_max=priv->idxth->dts-1;
	  priv->idxht->next=priv->idxth;
	}
	else priv->idxht->dts_max=frame_to_dts(cdata,cdata->nframes)-1;
	nidx=priv->idxht; // this is the value we will return

	// cross the pointers
	priv->idxht=index_walk(priv->idxht,mid_dts*4/3);
	priv->idxth=index_walk(priv->idxhh,mid_dts*2/3);

	return nidx;
      }

      // we crossed the mid point but head list is too short
      return index_upto(cdata,pts); // index from tail of head list up to pts
    }

    // TODO *** - for non-avc, may need to find next non-control video packet

    // add keyframe
    nidx=(index_entry *)malloc(sizeof(index_entry));
    nidx->offs=priv->input_position-11;
    nidx->dts=pack.dts;
    if (priv->idxth!=NULL) {
      nidx->dts_max=priv->idxth->dts-1;
      nidx->next=priv->idxth;
    }
    else {
      nidx->dts_max=frame_to_dts(cdata,cdata->nframes)-1;
      nidx->next=NULL;
    }
    
    priv->idxth=nidx;
    if (nidx->dts<=pts) break; // found what we were looking for

    lseek(priv->fd,-17,SEEK_CUR);
    tagsize=get_last_tagsize(cdata);
    priv->input_position-=15+tagsize;

  }

  return nidx;
}




static index_entry *get_idx_for_pts(const lives_clip_data_t *cdata, int64_t pts) {
  lives_flv_priv_t *priv=cdata->priv;
  int ldts;
  index_entry *idxhh=priv->idxhh;
  index_entry *idxth=priv->idxth;
  index_entry *idxht=priv->idxht;

  if (idxht!=NULL&&pts>=idxht->dts&&pts<=idxht->dts_max) return idxht;
  if (idxth!=NULL&&pts>=idxth->dts&&pts<=idxth->dts_max) return idxth;
  if (idxhh!=NULL&&pts<=idxhh->dts_max) return idxhh;

  if (idxht!=NULL&&idxth!=NULL&&idxht->dts>idxth->dts) {
    // list is complete, pointers are to 1/3 and 2/3 points (swapped to indicate complete)
    if (pts<idxth->dts) return index_walk(idxhh->next,pts); // in first 1/3
    if (pts<idxht->dts) return index_walk(idxth->next,pts); // in second 1/3
    return index_walk(idxht->next,pts); // in last 1/3
  }

  ldts=frame_to_dts(cdata,cdata->nframes-1)>>1; // halfway dts

  if (pts<=ldts) {
    // in first half
    if (idxhh!=NULL&&pts<idxht->dts) return index_walk(idxhh->next,pts); // somewhere in head list
    return index_upto(cdata,pts); // index from tail of head list up to pts
  }

  // in second half
  if (idxth!=NULL&&pts>idxth->dts_max) return index_walk(idxth->next,pts);
  else return index_downto(cdata,pts);


}


//////////////////////////////////////////////


static int flv_get_extradata(lives_clip_data_t *cdata, int size) {
  int dummy;
  lives_flv_priv_t *priv=cdata->priv;

  av_free(priv->ctx->extradata);
  priv->ctx->extradata = av_mallocz(size + FF_INPUT_BUFFER_PADDING_SIZE);
  if (priv->ctx->extradata==NULL) return AVERROR(ENOMEM);
  priv->ctx->extradata_size = size;
  dummy=read(priv->fd, priv->ctx->extradata, priv->ctx->extradata_size);
  return 0;
}


static int amf_get_string(unsigned char *inp, char *buf, size_t size) {
  size_t len=(inp[0]<<8)+inp[1];

  if (len>=size) {
    memset(buf,0,1);
    return -1;
  }

  memcpy(buf,inp+2,len);
  memset(buf+len,0,1);
  return len+2;
}


static void detach_stream (lives_clip_data_t *cdata) {
  // close the file, free the decoder
  lives_flv_priv_t *priv=cdata->priv;

  cdata->seek_flag=0;

  if (priv->ctx!=NULL) {
    avcodec_close(priv->ctx);
    av_free(priv->ctx);
  }

  if (priv->picture!=NULL) av_free(priv->picture);

  priv->ctx=NULL;
  priv->codec=NULL;
  priv->picture=NULL;

  if (priv->idxth!=NULL&&(priv->idxht==NULL||(priv->idxth->dts>priv->idxht->dts))) 
    index_free(priv->idxth);
  if (priv->idxhh!=NULL) index_free(priv->idxhh);

  priv->idxhh=NULL;
  priv->idxht=NULL;
  priv->idxth=NULL;

  if (cdata->palettes!=NULL) free(cdata->palettes);

  close(priv->fd);
}



static boolean attach_stream(lives_clip_data_t *cdata) {
  // open the file and get a handle
  lives_flv_priv_t *priv=cdata->priv;
  lives_flv_pack_t pack;
  char header[FLV_PROBE_SIZE];
  char buffer[FLV_META_SIZE];
  unsigned char flags,avctype;
  int type,size,vcodec=0,ldts;
  boolean gotmeta=FALSE,in_array=FALSE;
  boolean haskeyframes=FALSE,canseekend=FALSE,hasaudio;
  boolean got_astream=FALSE,got_vstream=FALSE;
  char *key=NULL;
  size_t offs=0;
  double num_val,fps,lasttimestamp=-1.;

  AVCodec *codec=NULL;
  AVCodecContext *ctx;

  boolean got_picture=FALSE,got_avcextradata=FALSE;

  //#define DEBUG
#ifdef DEBUG
  fprintf(stderr,"\n");
#endif

  if ((priv->fd=open(cdata->URI,O_RDONLY))==-1) {
    fprintf(stderr, "flv_decoder: unable to open %s\n",cdata->URI);
    return FALSE;
  }

  if (read (priv->fd, header, FLV_PROBE_SIZE) < FLV_PROBE_SIZE) {
    // for example, might be a directory
#ifdef DEBUG
    fprintf(stderr, "flv_decoder: unable to read header for %s\n",cdata->URI);
#endif
    close(priv->fd);
    return FALSE;
  }

  if (header[0] != 'F' || header[1] != 'L' || header[2] != 'V' || header[3] >= 5 || header[5]!=0) {
    close(priv->fd);
    return FALSE;
  }

  flags=header[4];

  if (!(flags&FLV_HEADER_FLAG_HASVIDEO)) {
    close(priv->fd);
    return FALSE;
  }

  hasaudio=flags&FLV_HEADER_FLAG_HASAUDIO;

  priv->input_position=4+((header[5]&0xFF)<<24)+((header[6]&0xFF)<<16)+((header[7]&0xFF)<<8)+((header[8]&0xFF));

  cdata->fps=0.;
  cdata->width=cdata->frame_width=cdata->height=cdata->frame_height=0;
  cdata->offs_x=cdata->offs_y=0;

  cdata->arate=0;
  cdata->achans=0;
  cdata->asamps=16;

  priv->idxhh=NULL;
  priv->idxht=NULL;
  priv->idxth=NULL;

  if (!lives_flv_parse_pack_header(cdata,&pack)) {
    close(priv->fd);
    return FALSE;
  }

  if (pack.type==FLV_TAG_TYPE_META) {
    // first packet should be metadata, but not always

    if (pack.size<19) {
      fprintf(stderr, "flv_decoder: invalid metadata for %s\n",cdata->URI);
      close(priv->fd);
      return FALSE;
    }

    pack.data=malloc(pack.size);
    if (read (priv->fd, pack.data, pack.size) < pack.size) {
      fprintf(stderr, "flv_decoder: error in metadata for %s\n",cdata->URI);
      free(pack.data);
      close(priv->fd);
      return FALSE;
    }

    priv->input_position+=pack.size+4; // 4 bytes for backwards size

    while (offs<pack.size-2) {
      if (in_array&&key==NULL) type=AMF_DATA_TYPE_STRING;
      else {
	type=(int)(pack.data[offs]&0xFF);
	offs++;
      }

      switch (type) {
      case AMF_DATA_TYPE_NULL:
      case AMF_DATA_TYPE_UNDEFINED:
      case AMF_DATA_TYPE_UNSUPPORTED:
	break; //these take up no additional space


      case AMF_DATA_TYPE_STRING:
      case AMF_DATA_TYPE_LONG_STRING:
      case AMF_DATA_TYPE_OBJECT:
	size=amf_get_string(pack.data+offs, buffer, FLV_META_SIZE);
	if (size<=0) size=10000000;
	offs+=size;
	if (!gotmeta) {
	  if (!strcmp(buffer, "onMetaData")) {
	    gotmeta=TRUE;
	  }
	}

	// deal with string
	if (key!=NULL) {
	  free(key);
	  key=NULL;
#ifdef DEBUG
	  fprintf(stderr,"%s\n",buffer);
#endif

	  // read eoo
	  if (!in_array) offs++;
	}
	else {
	  key=strdup(buffer);
#ifdef DEBUG
	  fprintf(stderr,"%s:",key);
#endif
	}
	break;

      case AMF_DATA_TYPE_MIXEDARRAY:
#ifdef DEBUG
	fprintf(stderr,"mixed");
#endif
	in_array=TRUE;
      case AMF_DATA_TYPE_ARRAY:
#ifdef DEBUG
	fprintf(stderr,"array\n");
#endif

	// TODO *** - check for "keyframes" (2 arrays, "filepositions", "times") and "seekPoints" (timestamps in ms)


	offs+=4; // max array elem
	if (key!=NULL) free(key);
	key=NULL;
	//eoo++;
	break;

      case AMF_DATA_TYPE_NUMBER:
	num_val = getfloat64(&pack.data[offs]);
#ifdef DEBUG
	fprintf(stderr,"%f\n",num_val);
#endif

	offs+=8;

	if (!strcmp(key,"framerate")) cdata->fps=num_val;
	if (!strcmp(key,"videoframerate")) cdata->fps=num_val;
	if (!strcmp(key,"audiosamplerate")) cdata->arate=num_val;
	if (!strcmp(key,"audiosamplesize")) cdata->asamps=num_val;
	if (!strcmp(key,"lasttimestamp")) lasttimestamp=num_val;
	if (!strcmp(key,"height")) cdata->height=num_val;
	if (!strcmp(key,"width")) cdata->width=num_val;

	if (key!=NULL) free(key);
	key=NULL;
	break;
      
      case AMF_DATA_TYPE_BOOL:
	num_val = (int)((pack.data[offs]&0xFF));
#ifdef DEBUG
	fprintf(stderr,"%s\n",((int)num_val==1)?"true":"false");
#endif

	offs+=1;

	if (!strcmp(key,"hasKeyframes")&&num_val==1.) haskeyframes=TRUE;
	else if (!strcmp(key,"canSeekToEnd")&&num_val==1.) canseekend=TRUE;
	else if (!strcmp(key,"hasAudio")&&num_val==0.) hasaudio=FALSE;
	if (!strcmp(key,"stereo")&&num_val==1.) cdata->achans=2;

	if (key!=NULL) free(key);
	key=NULL;

	break;

      case AMF_DATA_TYPE_DATE:
	offs+=10;
	if (key!=NULL) free(key);
	key=NULL;
#ifdef DEBUG
	fprintf(stderr,"\n");
#endif
	break;
      
      default:
	if (key!=NULL) free(key);
	key=NULL;
	break;

      }

    }

    free(pack.data);

    if (!gotmeta) {
      fprintf(stderr, "flv_decoder: no metadata found for %s\n",cdata->URI);
      close(priv->fd);
      return FALSE;
    }

    /*  if (!haskeyframes) {
	fprintf(stderr, "flv_decoder: non-seekable file %s\n",cdata->URI);
	close(priv->fd);
	return FALSE;
	} */


  }
  else priv->input_position-=11;

  cdata->seek_flag=LIVES_SEEK_FAST|LIVES_SEEK_NEEDS_CALCULATION;

  cdata->offs_x=0;
  cdata->offs_y=0;

  priv->data_start=priv->input_position;

  if (!hasaudio) got_astream=TRUE;

  priv->ctx=ctx = avcodec_alloc_context();

  // now we get the stream data
  while (!got_astream || !got_vstream) {
    do {
      if (!lives_flv_parse_pack_header(cdata,&pack)) {
	close(priv->fd);
	return FALSE;
      }
      if (pack.size==0) priv->input_position+=4; // backwards size
    } while (pack.size==0);
    
  
    pack.data=malloc(8);  // we only need at most 8 bytes here
    if (read (priv->fd, pack.data, 8) < 8) {
      fprintf(stderr, "flv_decoder: error in stream header for %s\n",cdata->URI);
      free(pack.data);
      close(priv->fd);
      return FALSE;
    }

    priv->input_position+=pack.size+4;

    // audio just for info
    if (pack.type==FLV_TAG_TYPE_AUDIO) {
      int acodec;
      flags=pack.data[0];
      acodec=flags&FLV_AUDIO_CODECID_MASK;
      switch (acodec) {
      case 0:
	// platform endian
	sprintf(cdata->audio_name,"%s","pcm"); break;
      case 1:
	sprintf(cdata->audio_name,"%s","adpcm"); break;
      case 14:
	cdata->arate=8192;
      case 2:
	sprintf(cdata->audio_name,"%s","mp3"); break;
      case 3:
	// little endian
	sprintf(cdata->audio_name,"%s","pcm"); break;
      case 4:
      case 5:
      case 6:
	sprintf(cdata->audio_name,"%s","nellymoser"); break;
      case 7:
      case 8:
	sprintf(cdata->audio_name,"%s","logpcm"); break;
      case 10:
	sprintf(cdata->audio_name,"%s","aac"); break;
      case 11:
	sprintf(cdata->audio_name,"%s","speex"); break;
      default:
	sprintf(cdata->audio_name,"%s","unknown"); break;
      }

      if (cdata->arate==0) {
	unsigned char aratec=flags&FLV_AUDIO_SAMPLERATE_MASK;
	cdata->arate=44100;
	if (aratec<3) cdata->arate>>=1;
	if (aratec<2) cdata->arate>>=1;
	if (aratec==0) cdata->arate=5500; 
      }

      if (cdata->achans==0) {
	cdata->achans=2;
	if ((flags&FLV_AUDIO_CHANNEL_MASK)==0) cdata->achans=1;
      }


      if (cdata->asamps==0) {
	cdata->asamps=16;
	if ((flags&FLV_AUDIO_SAMPLESIZE_MASK)==0) cdata->asamps=8;
      }

      got_astream=TRUE;
    }

    // only care about video here

    if (pack.type==FLV_TAG_TYPE_VIDEO) {
      flags=pack.data[0];

      if ((flags & 0xF0)==0x50) { // video info / command frame
	free(pack.data);
	continue;
      }


      if (got_vstream) {
#ifdef DEBUG
	fprintf(stderr,"flv_decoder: got duplicate video stream in %s\n",cdata->URI);
#endif
	free(pack.data);
	continue;
      }
      got_vstream=TRUE;
      priv->data_start=priv->input_position-pack.size-4-11;

      vcodec = flags & FLV_VIDEO_CODECID_MASK;

      // let avcodec do some of the work now

      priv->pack_offset=0;

      switch (vcodec) {
      case FLV_CODECID_H263  : sprintf(cdata->video_name,"%s","flv1");
	codec = avcodec_find_decoder(CODEC_ID_FLV1);
	priv->pack_offset=1;
	break;
      case FLV_CODECID_SCREEN: sprintf(cdata->video_name,"%s","flashsv"); 
	codec = avcodec_find_decoder(CODEC_ID_FLASHSV);
	priv->pack_offset=1;
	break;
      case FLV_CODECID_SCREEN2: sprintf(cdata->video_name,"%s","flashsv2"); 
	codec = avcodec_find_decoder(CODEC_ID_FLASHSV2);
	priv->pack_offset=1;
	break;
      case FLV_CODECID_VP6   : sprintf(cdata->video_name,"%s","vp6f"); 
	cdata->offs_x=(pack.data[1]&0X0F)>>1; // divide by 2 for offset
	cdata->offs_y=(pack.data[1]&0XF0)>>5; // divide by 2 for offset
	if (cdata->width==0) cdata->width=pack.data[7]*16-cdata->offs_x*2;
	if (cdata->height==0) cdata->height=pack.data[6]*16-cdata->offs_y*2;
	codec = avcodec_find_decoder(CODEC_ID_VP6F);
	priv->pack_offset=2;
	break;
      case FLV_CODECID_VP6A  :
	sprintf(cdata->video_name,"%s","vp6a");
	cdata->offs_x=(pack.data[1]&0X0F)>>1; // divide by 2 for offset
	cdata->offs_y=(pack.data[1]&0XF0)>>5; // divide by 2 for offset
	if (cdata->width==0) cdata->width=pack.data[7]*16-cdata->offs_x*2;
	if (cdata->height==0) cdata->height=pack.data[6]*16-cdata->offs_y*2;
	if (ctx->extradata_size != 1) {
	  ctx->extradata_size = 1;
	  ctx->extradata = av_malloc(1);
	}
	ctx->extradata[0] = pack.data[1];

	codec = avcodec_find_decoder(CODEC_ID_VP6A);
	priv->pack_offset=2;
	break;
      case FLV_CODECID_H264:
	// broken....
	sprintf(cdata->video_name,"%s","h264");
	codec = avcodec_find_decoder(CODEC_ID_H264);
	priv->pack_offset=5;
	break;
      default:
	fprintf(stderr,"flv_decoder: unknown video stream type (%d) in %s\n",vcodec,cdata->URI);
	memset(cdata->video_name,0,1);
	break;
      }

    }

    free(pack.data);

  }

#ifdef DEBUG
  fprintf(stderr,"video type is %s %d x %d (%d x %d +%d +%d)\n",cdata->video_name,
	  cdata->width,cdata->height,cdata->frame_width,cdata->frame_height,cdata->offs_x,cdata->offs_y);
#endif


  if (!codec) {
    if (strlen(cdata->video_name)>0) 
      fprintf(stderr, "flv_decoder: Could not find avcodec codec for video type %s\n",cdata->video_name);
    detach_stream(cdata);
    return FALSE;
  }

  if (avcodec_open(ctx, codec) < 0) {
    fprintf(stderr, "flv_decoder: Could not open avcodec context\n");
    detach_stream(cdata);
    return FALSE;
  }

  priv->codec=codec;

  // re-scan with avcodec; priv->data_start holds video data start position
  priv->input_position=priv->data_start;

  lives_flv_parse_pack_header(cdata,&pack);

  if (vcodec==FLV_CODECID_H264) {
    // check for extradata
    while (1) {

      priv->input_position+=pack.size+4;

      if (pack.size==0) {
	lives_flv_parse_pack_header(cdata,&pack);
	continue;
      }

      if (pack.type!=FLV_TAG_TYPE_VIDEO) {
	lives_flv_parse_pack_header(cdata,&pack);
	continue;
      }

      // read 5 bytes
      pack.data=malloc(5);
      if (read (priv->fd, pack.data, 5) < 5) {
	fprintf(stderr, "flv_decoder: error in stream header for %s\n",cdata->URI);
	free(pack.data);
	detach_stream(cdata);
	return FALSE;
      }
      
      flags=pack.data[0];
      avctype=pack.data[1];
      
      if ((flags & 0xF0)==0x50) { // video info / command frame
	free(pack.data);
	lives_flv_parse_pack_header(cdata,&pack);
	continue;
      }

      if (avctype==1&&got_avcextradata) {
	// got first real video packet
	priv->input_position-=(pack.size+4);
	priv->data_start=priv->input_position-11;
	free(pack.data);
	break;
      }

      if (avctype==0) { // avc header for h264
#ifdef DEBUG
	fprintf(stderr,"getting extradata size %d\n",pack.size-5);
#endif
	flv_get_extradata(cdata,pack.size-5);
	free(pack.data);
	got_avcextradata=TRUE;
	lives_flv_parse_pack_header(cdata,&pack);
	continue;
      }


    }
  }
  else if (priv->pack_offset!=0) lseek(priv->fd,priv->pack_offset,SEEK_CUR);

  pack.data=malloc(pack.size-priv->pack_offset+FF_INPUT_BUFFER_PADDING_SIZE);

  av_init_packet(&priv->avpkt);

  priv->avpkt.size=read (priv->fd, pack.data, pack.size-priv->pack_offset);
  memset(pack.data+priv->avpkt.size,0,FF_INPUT_BUFFER_PADDING_SIZE);
  priv->input_position+=pack.size+4;
  priv->avpkt.data = pack.data;
  priv->avpkt.dts=priv->avpkt.pts=pack.dts;

  priv->picture = avcodec_alloc_frame();

  while (!got_picture) {
    int len;

    // this does not work for h264 and I dont know why not !!!!!!!!!!!!!!!!!!!!
#if LIBAVCODEC_VERSION_MAJOR >= 53
    len=avcodec_decode_video2(ctx, priv->picture, &got_picture, &priv->avpkt );
#else 
    len=avcodec_decode_video(ctx, priv->picture, &got_picture, priv->avpkt.data, priv->avpkt.size );
#endif

    if (!got_picture) {
      break;  // code below should pull more packets for h264, but still does not work !!!!!!

      // pull next video packet
 
      while (1) {
	lives_flv_parse_pack_header(cdata,&pack);
	priv->input_position+=pack.size+4;
	if (pack.size==0) continue;

	if (pack.type==FLV_TAG_TYPE_VIDEO) {

	  pack.data=malloc(5);  // we only need at most 5 bytes here
	  if (read (priv->fd, pack.data, 5) < 5) {
	    fprintf(stderr, "flv_decoder: error in stream header for %s\n",cdata->URI);
	    free(pack.data);
	    close(priv->fd);
	    return FALSE;
	  }

	  flags=pack.data[0];
	  avctype=pack.data[1];
	  
	  if ((flags & 0xF0)==0x50) { // video info / command frame
	    free(pack.data);
	    continue;
	  }

	  if (avctype==0) { // avc header for h264
	    printf("getting extradata %d %d\n",ctx->extradata_size,pack.size);
	    flv_get_extradata(cdata,pack.size-5);
	    free(pack.data);
	    continue;
	  }

	  if (avctype!=1) { // avc header for h264
	    free(pack.data);
	    continue;
	  }

	  pack.data=malloc(pack.size-priv->pack_offset+FF_INPUT_BUFFER_PADDING_SIZE);
	  if (priv->pack_offset!=5) lseek(priv->fd,priv->pack_offset-5,SEEK_CUR);
	  priv->avpkt.size=read (priv->fd, pack.data, pack.size-priv->pack_offset);
	  memset(pack.data+priv->avpkt.size,0,FF_INPUT_BUFFER_PADDING_SIZE);
	  priv->avpkt.data = pack.data;
	  break;
	}
      }
    }

  }

  free(pack.data);

  if (!got_picture) {
    fprintf(stderr,"flv_decoder: could not get picture.\n");
    detach_stream(cdata);
    return FALSE;
  }

  cdata->YUV_clamping=WEED_YUV_CLAMPING_UNCLAMPED;
  if (ctx->color_range==AVCOL_RANGE_MPEG) cdata->YUV_clamping=WEED_YUV_CLAMPING_CLAMPED;

  cdata->YUV_sampling=WEED_YUV_SAMPLING_DEFAULT;
  if (ctx->chroma_sample_location!=AVCHROMA_LOC_LEFT) cdata->YUV_sampling=WEED_YUV_SAMPLING_MPEG;

  cdata->YUV_subspace=WEED_YUV_SUBSPACE_YCBCR;
  if (ctx->colorspace==AVCOL_SPC_BT709) cdata->YUV_subspace=WEED_YUV_SUBSPACE_BT709;

  cdata->palettes=(int *)malloc(2*sizeof(int));

  cdata->palettes[0]=pix_fmt_to_palette(ctx->pix_fmt,&cdata->YUV_clamping);
  cdata->palettes[1]=WEED_PALETTE_END;

  if (cdata->palettes[0]==WEED_PALETTE_END) {
    fprintf(stderr, "flv_decoder: Could not find a usable palette for (%d) %s\n",ctx->pix_fmt,cdata->URI);
    detach_stream(cdata);
    return FALSE;
  }

  cdata->current_palette=cdata->palettes[0];

  // re-get fps, width, height, nframes - actually avcodec is pretty useless at getting this
  // so we fall back on the values we obtained ourselves

  if (cdata->width==0) cdata->width=ctx->width-cdata->offs_x*2;
  if (cdata->height==0) cdata->height=ctx->height-cdata->offs_y*2;
  
  if (cdata->width*cdata->height==0) {
    fprintf(stderr, "flv_decoder: invalid width and height (%d X %d)\n",cdata->width,cdata->height);
    detach_stream(cdata);
    return FALSE;
  }

#ifdef DEBUG
  fprintf(stderr,"using palette %d, size %d x %d\n",
	  cdata->current_palette,cdata->width,cdata->height);
#endif

  cdata->par=(double)ctx->sample_aspect_ratio.num/(double)ctx->sample_aspect_ratio.den;
  if (cdata->par==0.) cdata->par=1.;

  if (ctx->time_base.den>0&&ctx->time_base.num>0) {
    fps=(double)ctx->time_base.den/(double)ctx->time_base.num;
    if (fps!=1000.) cdata->fps=fps;
  }

  if (cdata->fps==0.&&ctx->time_base.num==0) {
    // not sure about this
    if (ctx->time_base.den==1) cdata->fps=12.;
  }

  if (cdata->fps==0.||cdata->fps==1000.) {
    fprintf(stderr, "flv_decoder: invalid framerate %.4f (%d / %d)\n",cdata->fps,ctx->time_base.den,ctx->time_base.num);
    detach_stream(cdata);
    return FALSE;
  }
  
  if (ctx->ticks_per_frame==2) {
    // TODO - needs checking
    cdata->fps/=2.;
    cdata->interlace=LIVES_INTERLACE_BOTTOM_FIRST;
  }

  priv->last_frame=-1;

  ldts=get_last_video_dts(cdata);

  if (ldts==-1) {
    fprintf(stderr, "flv_decoder: could not read last dts\n");
    detach_stream(cdata);
    return FALSE;
  }

  cdata->nframes=dts_to_frame(cdata,ldts)+1;

#ifdef DEBUG
  fprintf(stderr,"fps is %.4f %ld\n",cdata->fps,cdata->nframes);
#endif


  return TRUE;
}


//////////////////////////////////////////
// std functions



const char *module_check_init(void) {
  avcodec_init();
  avcodec_register_all();
  return NULL;
}


const char *version(void) {
  return plugin_version;
}



static lives_clip_data_t *init_cdata (void) {
  lives_flv_priv_t *priv;
  lives_clip_data_t *cdata=(lives_clip_data_t *)malloc(sizeof(lives_clip_data_t));

  cdata->URI=NULL;
  
  cdata->priv=priv=malloc(sizeof(lives_flv_priv_t));

  cdata->seek_flag=0;

  priv->ctx=NULL;
  priv->codec=NULL;
  priv->picture=NULL;

  cdata->palettes=NULL;
  
  return cdata;
}






lives_clip_data_t *get_clip_data(const char *URI, lives_clip_data_t *cdata) {
  // the first time this is called, caller should pass NULL as the cdata
  // subsequent calls to this should re-use the same cdata

  // if the host wants a different current_clip, this must be called again with the same
  // cdata as the second parameter

  // value returned should be freed with clip_data_free() when no longer required

  // should be thread-safe

  lives_flv_priv_t *priv;


  if (cdata!=NULL&&cdata->current_clip>0) {
    // currently we only support one clip per container

    clip_data_free(cdata);
    return NULL;
  }

  if (cdata==NULL) {
    cdata=init_cdata();
  }

  if (cdata->URI==NULL||strcmp(URI,cdata->URI)) {
    if (cdata->URI!=NULL) {
      detach_stream(cdata);
      free(cdata->URI);
    }
    cdata->URI=strdup(URI);
    if (!attach_stream(cdata)) {
      free(cdata->URI);
      cdata->URI=NULL;
      clip_data_free(cdata);
      return NULL;
    }
    cdata->current_palette=cdata->palettes[0];
    cdata->current_clip=0;
  }

  cdata->nclips=1;

  ///////////////////////////////////////////////////////////

  sprintf(cdata->container_name,"%s","flv");

  // cdata->height was set when we attached the stream

  cdata->interlace=LIVES_INTERLACE_NONE;

  cdata->frame_width=cdata->width+cdata->offs_x*2;
  cdata->frame_height=cdata->height+cdata->offs_y*2;

  priv=cdata->priv;
  // TODO - check this = spec suggests we should cut right and bottom
  if (priv->ctx->width==cdata->frame_width) cdata->offs_x=0;
  if (priv->ctx->height==cdata->frame_height) cdata->offs_y=0;

  ////////////////////////////////////////////////////////////////////

  cdata->asigned=TRUE;
  cdata->ainterleaf=TRUE;

  return cdata;
}

static size_t write_black_pixel(unsigned char *idst, int pal, int npixels, int y_black) {
  unsigned char *dst=idst;
  register int i;

  for (i=0;i<npixels;i++) {
    switch (pal) {
    case WEED_PALETTE_RGBA32:
    case WEED_PALETTE_BGRA32:
      dst[0]=dst[1]=dst[2]=0;
      dst[3]=255;
      dst+=4;
      break;
    case WEED_PALETTE_ARGB32:
      dst[1]=dst[2]=dst[3]=0;
      dst[0]=255;
      dst+=4;
      break;
    case WEED_PALETTE_UYVY8888:
      dst[1]=dst[3]=y_black;
      dst[0]=dst[2]=128;
      dst+=4;
      break;
    case WEED_PALETTE_YUYV8888:
      dst[0]=dst[2]=y_black;
      dst[1]=dst[3]=128;
      dst+=4;
      break;
    case WEED_PALETTE_YUV888:
      dst[0]=y_black;
      dst[1]=dst[2]=128;
      dst+=3;
      break;
    case WEED_PALETTE_YUVA8888:
      dst[0]=y_black;
      dst[1]=dst[2]=128;
      dst[3]=255;
      dst+=4;
      break;
    case WEED_PALETTE_YUV411:
      dst[0]=dst[3]=128;
      dst[1]=dst[2]=dst[4]=dst[5]=y_black;
      dst+=6;
    default: break;
    }
  }
  return idst-dst;
}


boolean get_frame(const lives_clip_data_t *cdata, int64_t tframe, int *rowstrides, int height, void **pixel_data) {
  // seek to frame,

  int64_t target_pts=frame_to_dts(cdata,tframe);
  int64_t nextframe=0;
  lives_flv_priv_t *priv=cdata->priv;
  lives_flv_pack_t pack;
  int xheight=cdata->frame_height,pal=cdata->current_palette,nplanes=1,dstwidth=cdata->width,psize=1;
  int btop=cdata->offs_y,bbot=xheight-1-btop;
  int bleft=cdata->offs_x,bright=cdata->frame_width-cdata->width-bleft;
  int rescan_limit=16;  // pick some arbitrary value
  int y_black=(cdata->YUV_clamping==WEED_YUV_CLAMPING_CLAMPED)?16:0;
  boolean got_picture=FALSE;
  unsigned char *dst,*src,flags;
  unsigned char black[4]={0,0,0,255};
  index_entry *idx;
  register int i,p;

#ifdef DEBUG_KFRAMES
  fprintf(stderr,"vals %ld %ld\n",tframe,priv->last_frame);
#endif

  // calc frame width and height, including any border

  if (pal==WEED_PALETTE_YUV420P||pal==WEED_PALETTE_YVU420P||pal==WEED_PALETTE_YUV422P||pal==WEED_PALETTE_YUV444P) {
    nplanes=3;
    black[0]=y_black;
    black[1]=black[2]=128;
  }
  else if (pal==WEED_PALETTE_YUVA4444P) {
    nplanes=4;
    black[0]=y_black;
    black[1]=black[2]=128;
    black[3]=255;
  }
  
  if (pal==WEED_PALETTE_RGB24||pal==WEED_PALETTE_BGR24) psize=3;

  if (pal==WEED_PALETTE_RGBA32||pal==WEED_PALETTE_BGRA32||pal==WEED_PALETTE_ARGB32||pal==WEED_PALETTE_UYVY8888||pal==WEED_PALETTE_YUYV8888||pal==WEED_PALETTE_YUV888||pal==WEED_PALETTE_YUVA8888) psize=4;

  if (pal==WEED_PALETTE_YUV411) psize=6;

  if (pal==WEED_PALETTE_A1) dstwidth>>=3;

  dstwidth*=psize;

  if (cdata->frame_height > cdata->height && height == cdata->height) {
    // host ignores vertical border
    btop=0;
    xheight=cdata->height;
    bbot=xheight-1;
  }

  if (cdata->frame_width > cdata->width && rowstrides[0] < cdata->frame_width*psize) {
    // host ignores horizontal border
    bleft=bright=0;
  }

  ////////////////////////////////////////////////////////////////////

  if (tframe!=priv->last_frame) {

    if (priv->last_frame==-1 || (tframe<priv->last_frame) || (tframe - priv->last_frame > rescan_limit)) {

      if ((idx=get_idx_for_pts(cdata,target_pts))!=NULL) {
	priv->input_position=idx->offs;
	nextframe=dts_to_frame(cdata,idx->dts);
      }
      else priv->input_position=priv->data_start;
      
      // we are now at the kframe before or at target - parse packets until we hit target
      

#ifdef DEBUG_KFRAMES
      if (idx!=NULL) printf("got kframe %ld for frame %ld\n",dts_to_frame(cdata,idx->dts),tframe);
#endif

      avcodec_flush_buffers (priv->ctx);

    }
    else {
      nextframe=priv->last_frame+1;
    }

    priv->ctx->skip_frame=AVDISCARD_NONREF;

    priv->last_frame=tframe;


    // do this until we reach target frame //////////////

    do {

      // skip_idct and skip_frame. ???

      if (!lives_flv_parse_pack_header(cdata,&pack)) return FALSE;

      priv->input_position+=pack.size+4;

      if (pack.type!=FLV_TAG_TYPE_VIDEO) continue;

      if (read (priv->fd, &flags, 1) < 1) return FALSE;

      if ((flags & 0xF0)==0x50) { // video info / command frame
	continue;
      }

      pack.data=malloc(pack.size-priv->pack_offset+FF_INPUT_BUFFER_PADDING_SIZE);

      if (priv->pack_offset!=1) lseek(priv->fd,priv->pack_offset-1,SEEK_CUR);

      priv->avpkt.size=read (priv->fd, pack.data, pack.size-priv->pack_offset);
      memset(pack.data+priv->avpkt.size,0,FF_INPUT_BUFFER_PADDING_SIZE);
      priv->avpkt.data = pack.data;
      priv->avpkt.dts=priv->avpkt.pts=pack.dts;

      got_picture=FALSE;

      if ((pack.data[0] & FLV_VIDEO_CODECID_MASK)==FLV_CODECID_VP6A) {
	priv->ctx->extradata[0] = pack.data[1];
      }

      if (nextframe<tframe&&(pack.data[0] & FLV_VIDEO_CODECID_MASK)==FLV_CODECID_H263
	  &&(pack.data[0] & FLV_VIDEO_FRAMETYPE_MASK)==0x03) {
	// disposable intra-frame
	free(pack.data);
	nextframe++;
	continue;
      }

      if (nextframe==tframe) priv->ctx->skip_frame=AVDISCARD_DEFAULT;

      while (!got_picture) {
	int len;
#if LIBAVCODEC_VERSION_MAJOR >= 52
	len=avcodec_decode_video2(priv->ctx, priv->picture, &got_picture, &priv->avpkt );
#else 
	len=avcodec_decode_video(priv->ctx, priv->picture, &got_picture, priv->avpkt.data, priv->avpkt.size );
#endif
	priv->avpkt.size-=len;
	priv->avpkt.data+=len;

	if (!got_picture&&priv->avpkt.size<=0) return FALSE;

      }

      free(pack.data);
      nextframe++;
    } while (nextframe<=tframe);

    /////////////////////////////////////////////////////

  }
  
  for (p=0;p<nplanes;p++) {
    dst=pixel_data[p];
    src=priv->picture->data[p];

    for (i=0;i<xheight;i++) {
      if (i<btop||i>bbot) {
	// top or bottom border, copy black row
	if (pal==WEED_PALETTE_YUV420P||pal==WEED_PALETTE_YVU420P||pal==WEED_PALETTE_YUV422P||pal==WEED_PALETTE_YUV444P||pal==WEED_PALETTE_YUVA4444P||pal==WEED_PALETTE_RGB24||pal==WEED_PALETTE_BGR24) {
	  memset(dst,black[p],dstwidth+(bleft+bright)*psize);
	  dst+=dstwidth+(bleft+bright)*psize;
	}
	else dst+=write_black_pixel(dst,pal,dstwidth/psize+bleft+bright,y_black);
	continue;
      }

      if (bleft>0) {
	if (pal==WEED_PALETTE_YUV420P||pal==WEED_PALETTE_YVU420P||pal==WEED_PALETTE_YUV422P||pal==WEED_PALETTE_YUV444P||pal==WEED_PALETTE_YUVA4444P||pal==WEED_PALETTE_RGB24||pal==WEED_PALETTE_BGR24) {
	  memset(dst,black[p],bleft*psize);
	  dst+=bleft*psize;
	}
	else dst+=write_black_pixel(dst,pal,bleft,y_black);
      }

      memcpy(dst,src,dstwidth);
      dst+=dstwidth;

      if (bright>0) {
	if (pal==WEED_PALETTE_YUV420P||pal==WEED_PALETTE_YVU420P||pal==WEED_PALETTE_YUV422P||pal==WEED_PALETTE_YUV444P||pal==WEED_PALETTE_YUVA4444P||pal==WEED_PALETTE_RGB24||pal==WEED_PALETTE_BGR24) {
	  memset(dst,black[p],bright*psize);
	  dst+=bright*psize;
	}
	else dst+=write_black_pixel(dst,pal,bright,y_black);
      }

      src+=priv->picture->linesize[p];
    }
    if (p==0&&(pal==WEED_PALETTE_YUV420P||pal==WEED_PALETTE_YVU420P||pal==WEED_PALETTE_YUV422P)) {
      dstwidth>>=1;
      bleft>>=1;
      bright>>=1;
    }
    if (p==0&&(pal==WEED_PALETTE_YUV420P||pal==WEED_PALETTE_YVU420P)) {
      xheight>>=1;
      btop>>=1;
      bbot>>=1;
    }
  }
  
  return TRUE;
}




void clip_data_free(lives_clip_data_t *cdata) {

  if (cdata->URI!=NULL) {
    detach_stream(cdata);
    free(cdata->URI);
  }

  free(cdata->priv);
  free(cdata);
}


void module_unload(void) {

}

int main(void) {
  // for testing
  module_check_init();
  get_clip_data("vid.flv",NULL);
  return 1;
}



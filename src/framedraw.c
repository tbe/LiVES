// framedraw.c
// LiVES
// (c) G. Finch (salsaman@gmail.com) 2002 - 2013
// see file COPYING for licensing details : released under the GNU GPL 3 or later

// functions for the 'framedraw' widget - lets users draw on frames :-)

#ifdef HAVE_SYSTEM_WEED
#include <weed/weed-palettes.h>
#else
#include "../libweed/weed-palettes.h"
#endif

#include "main.h"
#include "callbacks.h"
#include "support.h"
#include "interface.h"
#include "effects.h"
#include "cvirtual.h"
#include "framedraw.h"

// set by mouse button press
static double xstart,ystart;
static double xcurrent,ycurrent;
static volatile boolean b1_held;

static volatile boolean noupdate=FALSE;

static GtkWidget *fbord_eventbox;


static double calc_fd_scale(int width, int height) {
  double scale=1.;

 if (width<MIN_PRE_X) {
    width=MIN_PRE_X;
  }
  if (height<MIN_PRE_Y) {
    height=MIN_PRE_Y;
  }

  if (width>MAX_PRE_X) scale=(double)width/(double)MAX_PRE_X;
  if (height>MAX_PRE_Y&&(height/MAX_PRE_Y>scale)) scale=(double)height/(double)MAX_PRE_Y;
  return scale;

}

static void start_preview (GtkButton *button, lives_rfx_t *rfx) {
  int i;
  gchar *com;

  gtk_widget_set_sensitive(mainw->framedraw_preview,FALSE);
  while (g_main_context_iteration(NULL,FALSE));

  if (mainw->did_rfx_preview) {
#ifndef IS_MINGW
    com=g_strdup_printf("%s stopsubsub \"%s\" 2>/dev/null",prefs->backend_sync,cfile->handle);
    lives_system(com,TRUE); // try to stop any in-progress preview
#else
    // get pid from backend
    FILE *rfile;
    ssize_t rlen;
    char val[16];
    int pid;
    com=g_strdup_printf("%s get_pid_for_handle \"%s\"",prefs->backend_sync,cfile->handle);
    rfile=popen(com,"r");
    rlen=fread(val,1,16,rfile);
    pclose(rfile);
    memset(val+rlen,0,1);
    pid=atoi(val);
    
    lives_win32_kill_subprocesses(pid,TRUE);
#endif
    g_free(com);

    if (cfile->start==0) {
      cfile->start=1;
      cfile->end=cfile->frames;
    }

    do_rfx_cleanup(rfx);
  }

#ifndef IS_MINGW
  com=g_strdup_printf("%s clear_pre_files \"%s\" 2>/dev/null",prefs->backend_sync,cfile->handle);
#else
  com=g_strdup_printf("%s clear_pre_files \"%s\" 2>NUL",prefs->backend_sync,cfile->handle);
#endif
  lives_system(com,TRUE); // clear any .pre files from before

  for (i=0;i<rfx->num_params;i++) {
    rfx->params[i].changed=FALSE;
  }

  mainw->cancelled=CANCEL_NONE;
  mainw->error=FALSE;

  // within do_effect() we check and if  
  do_effect(rfx,TRUE); // actually start effect processing in the background

  gtk_widget_set_sensitive(mainw->framedraw_spinbutton,TRUE);
  gtk_widget_set_sensitive(mainw->framedraw_scale,TRUE);

  if (mainw->framedraw_frame>cfile->start&&!(cfile->start==0&&mainw->framedraw_frame==1)) 
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(mainw->framedraw_spinbutton),cfile->start);
  else {
    load_rfx_preview(rfx);
  }

  mainw->did_rfx_preview=TRUE;
}




void framedraw_connect_spinbutton(lives_special_framedraw_rect_t *framedraw, lives_rfx_t *rfx) {
  framedraw->rfx=rfx;

  g_signal_connect_after (GTK_OBJECT (mainw->framedraw_spinbutton), "value_changed",
			  G_CALLBACK (after_framedraw_frame_spinbutton_changed),
			  framedraw);

}



void framedraw_connect(lives_special_framedraw_rect_t *framedraw, int width, int height, lives_rfx_t *rfx) {


  // add mouse fn's so we can draw on frames
  g_signal_connect (GTK_OBJECT (mainw->framedraw), "motion_notify_event",
		    G_CALLBACK (on_framedraw_mouse_update),
		    framedraw);
  g_signal_connect (GTK_OBJECT (mainw->framedraw), "button_release_event",
		    G_CALLBACK (on_framedraw_mouse_reset),
		    framedraw);
  g_signal_connect (GTK_OBJECT (mainw->framedraw), "button_press_event",
		    G_CALLBACK (on_framedraw_mouse_start),
		    framedraw);
  g_signal_connect (GTK_OBJECT(mainw->framedraw), "enter-notify-event",G_CALLBACK (on_framedraw_enter),framedraw);
  g_signal_connect (GTK_OBJECT(mainw->framedraw), "leave-notify-event",G_CALLBACK (on_framedraw_leave),framedraw);

  framedraw_connect_spinbutton(framedraw,rfx);

  lives_widget_set_bg_color (mainw->fd_frame, GTK_STATE_NORMAL, &palette->light_red);
  lives_widget_set_bg_color (fbord_eventbox, GTK_STATE_NORMAL, &palette->light_red);

  framedraw_redraw(framedraw, TRUE, NULL);
}


void framedraw_add_label(GtkVBox *box) {
  GtkWidget *label;

  // TRANSLATORS - Preview refers to preview window; keep this phrase short
  label=lives_standard_label_new(_("You can click in Preview to change these values"));
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
}


void framedraw_add_reset(GtkVBox *box, lives_special_framedraw_rect_t *framedraw) {
  GtkWidget *hbox_rst;
 
  framedraw_add_label(box);

  mainw->framedraw_reset = gtk_button_new_from_stock ("gtk-refresh");
  hbox_rst = lives_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (box), hbox_rst, FALSE, FALSE, 0);
  
  gtk_button_set_label (GTK_BUTTON (mainw->framedraw_reset),_ ("_Reset Values"));
  gtk_button_set_use_underline (GTK_BUTTON (mainw->framedraw_reset), TRUE);
  gtk_box_pack_start (GTK_BOX (hbox_rst), mainw->framedraw_reset, TRUE, FALSE, 0);
  gtk_widget_set_sensitive (mainw->framedraw_reset,FALSE);
  
  g_signal_connect (mainw->framedraw_reset, "clicked",G_CALLBACK (on_framedraw_reset_clicked),framedraw);
}


static boolean expose_fd_event (GtkWidget *widget, GdkEventExpose ev) {
  redraw_framedraw_image();
  return TRUE;
}

void widget_add_framedraw (GtkVBox *box, int start, int end, boolean add_preview_button, int width, int height) {
  // adds the frame draw widget to box
  // the redraw button should be connected to an appropriate redraw function
  // after calling this function

  // an example of this is in 'trim frames'

  GtkWidget *vseparator;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *label;
  GtkAdjustment *spinbutton_adj;
  GtkWidget *frame;
 
  lives_rfx_t *rfx;

  double fd_scale;

  b1_held=FALSE;

  mainw->framedraw_reset=NULL;

  vseparator = lives_vseparator_new ();
  gtk_box_pack_start (GTK_BOX (lives_widget_get_parent(LIVES_WIDGET (box))), vseparator, FALSE, FALSE, 0);
  gtk_widget_show (vseparator);

  vbox = lives_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (lives_widget_get_parent(LIVES_WIDGET (box))), vbox, FALSE, FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 10);

  fd_scale=calc_fd_scale(width,height);
  width/=fd_scale;
  height/=fd_scale;
 
  hbox = lives_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  add_fill_to_box(GTK_BOX(hbox));

  fbord_eventbox=gtk_event_box_new();
  gtk_container_set_border_width(GTK_CONTAINER(fbord_eventbox),widget_opts.border_width);

  frame = gtk_frame_new (NULL);

  add_fill_to_box(GTK_BOX(hbox));
  gtk_box_pack_start (GTK_BOX (hbox), frame, FALSE, FALSE, 0);

  if (palette->style&STYLE_1) {
    lives_widget_set_bg_color (fbord_eventbox, GTK_STATE_NORMAL, &palette->normal_fore);
    lives_widget_set_bg_color (frame, GTK_STATE_NORMAL, &palette->normal_back);
    lives_widget_set_fg_color (frame, GTK_STATE_NORMAL, &palette->normal_fore);
  }
  gtk_frame_set_shadow_type (GTK_FRAME(frame), GTK_SHADOW_IN);

  mainw->fd_frame=frame;

  label = lives_standard_label_new (_("Preview"));
  gtk_frame_set_label_widget (GTK_FRAME (frame), label);

  gtk_frame_set_shadow_type (GTK_FRAME(frame), GTK_SHADOW_NONE);

  mainw->framedraw=gtk_event_box_new();
  gtk_widget_set_size_request (mainw->framedraw, width, height);
  gtk_container_set_border_width(GTK_CONTAINER(mainw->framedraw),1);

  gtk_widget_set_events (mainw->framedraw, GDK_BUTTON1_MOTION_MASK | GDK_BUTTON_RELEASE_MASK | 
			 GDK_BUTTON_PRESS_MASK| GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);

  mainw->framedraw_frame=start;

  gtk_container_add (GTK_CONTAINER (frame), fbord_eventbox);
  gtk_container_add (GTK_CONTAINER (fbord_eventbox), mainw->framedraw);

  if (palette->style&STYLE_1) {
    lives_widget_set_bg_color (mainw->framedraw, GTK_STATE_NORMAL, &palette->normal_back);
    lives_widget_set_fg_color (mainw->framedraw, GTK_STATE_NORMAL, &palette->normal_fore);
  }

  g_signal_connect_after (GTK_OBJECT (mainw->framedraw), LIVES_WIDGET_EVENT_EXPOSE_EVENT,
			  G_CALLBACK (expose_fd_event), NULL);


  hbox = lives_hbox_new (FALSE, 2);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  mainw->framedraw_spinbutton = lives_standard_spin_button_new (_("    _Frame"),
								TRUE,start,start,end,1.,10.,0,LIVES_BOX(hbox),NULL);

  spinbutton_adj=gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(mainw->framedraw_spinbutton));

  mainw->framedraw_scale=lives_hscale_new_with_range(0,1,1);
  gtk_box_pack_start (GTK_BOX (hbox), mainw->framedraw_scale, TRUE, TRUE, 0);
  gtk_range_set_adjustment(GTK_RANGE(mainw->framedraw_scale),spinbutton_adj);
  gtk_scale_set_draw_value(GTK_SCALE(mainw->framedraw_scale),FALSE);

  rfx=(lives_rfx_t *)g_object_get_data(G_OBJECT(gtk_widget_get_toplevel(GTK_WIDGET(box))),"rfx");
  mainw->framedraw_preview = gtk_button_new_from_stock ("gtk-refresh");
  gtk_button_set_label (GTK_BUTTON (mainw->framedraw_preview),_ ("_Preview"));
  gtk_button_set_use_underline (GTK_BUTTON (mainw->framedraw_preview), TRUE);
  gtk_box_pack_start (GTK_BOX (hbox), mainw->framedraw_preview, TRUE, FALSE, 0);
  gtk_widget_set_sensitive(mainw->framedraw_spinbutton,FALSE);
  gtk_widget_set_sensitive(mainw->framedraw_scale,FALSE);
  g_signal_connect (mainw->framedraw_preview, "clicked",G_CALLBACK (start_preview),rfx);
  
  gtk_widget_show_all (vbox);

  if (!add_preview_button) {
    gtk_widget_hide(mainw->framedraw_preview);
  }

}



void framedraw_redraw (lives_special_framedraw_rect_t * framedraw, boolean reload, GdkPixbuf *pixbuf) {
  // this will draw the mask (framedraw_bitmap) and optionally reload the image
  // and then combine them

  int fd_height;
  int fd_width;
  int width,height;

  double xstartf,ystartf,xendf,yendf;

  lives_painter_t *cr;

  if (mainw->current_file<1||cfile==NULL) return;
  
  if (framedraw->rfx->source_type==LIVES_RFX_SOURCE_RFX) 
    if (noupdate) return;

  fd_width=lives_widget_get_allocation_width(mainw->framedraw);
  fd_height=lives_widget_get_allocation_height(mainw->framedraw);
    
  width=cfile->hsize;
  height=cfile->vsize;
  
  calc_maxspect(fd_width,fd_height,&width,&height);

  // copy from orig, resize
  // copy orig layer to layer
  if (mainw->fd_layer!=NULL) {
    weed_layer_free(mainw->fd_layer);
    mainw->fd_layer=NULL;
  }
  
  if (reload||mainw->fd_layer_orig==NULL) load_framedraw_image(pixbuf);
  
  mainw->fd_layer=weed_layer_copy(NULL,mainw->fd_layer_orig);
  // resize to correct size
  resize_layer(mainw->fd_layer, width, height, LIVES_INTERP_BEST);
  
  cr=layer_to_lives_painter(mainw->fd_layer);


  // draw on the lives_painter

  // her we dont offset because we are drawing in the pixbuf, not the widget

  switch (framedraw->type) {
  case LIVES_PARAM_SPECIAL_TYPE_RECT_MULTRECT: // deprecated
    // scale values
    if (framedraw->xstart_param->dp==0) {
      xstartf=(double)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON (framedraw->xstart_param->widgets[0]));
      xstartf=xstartf/(double)cfile->hsize*(double)width;
    }
    else {
      xstartf=gtk_spin_button_get_value (GTK_SPIN_BUTTON (framedraw->xstart_param->widgets[0]));
      xstartf=xstartf*(double)width;
    }

    if (framedraw->xend_param->dp==0) {
      xendf=(double)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON (framedraw->xend_param->widgets[0]));
      xendf=xendf/(double)cfile->hsize*(double)width;
    }
    else {
      xendf=gtk_spin_button_get_value (GTK_SPIN_BUTTON (framedraw->xend_param->widgets[0]));
      xendf=xendf*(double)width;
    }

    if (framedraw->ystart_param->dp==0) {
      ystartf=(double)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON (framedraw->ystart_param->widgets[0]));
      ystartf=ystartf/(double)cfile->vsize*(double)height;
    }
    else {
      ystartf=gtk_spin_button_get_value (GTK_SPIN_BUTTON (framedraw->ystart_param->widgets[0]));
      ystartf=ystartf*(double)height;
    }

    if (framedraw->yend_param->dp==0) {
      yendf=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON (framedraw->yend_param->widgets[0]));
      yendf=yendf/(double)cfile->vsize*(double)height;
    }
    else {
      yendf=gtk_spin_button_get_value (GTK_SPIN_BUTTON (framedraw->yend_param->widgets[0]));
      yendf=yendf*(double)height;
    }

    lives_painter_set_source_rgb(cr, 1., 0., 0.);
    lives_painter_rectangle(cr,xstartf-1.,ystartf-1.,xendf+2.,yendf+2.);
    lives_painter_stroke (cr);

    break;
  case LIVES_PARAM_SPECIAL_TYPE_RECT_MULTIRECT:
  case LIVES_PARAM_SPECIAL_TYPE_RECT_DEMASK:

    if (framedraw->xstart_param->dp==0) {
      xstartf=(double)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON (framedraw->xstart_param->widgets[0]));
      xstartf=xstartf/(double)cfile->hsize*(double)width;
    }
    else {
      xstartf=gtk_spin_button_get_value (GTK_SPIN_BUTTON (framedraw->xstart_param->widgets[0]));
      xstartf=xstartf*(double)width;
    }

    if (framedraw->xend_param->dp==0) {
      xendf=(double)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON (framedraw->xend_param->widgets[0]));
      xendf=xendf/(double)cfile->hsize*(double)width;
    }
    else {
      xendf=gtk_spin_button_get_value (GTK_SPIN_BUTTON (framedraw->xend_param->widgets[0]));
      xendf=xendf*(double)width;
    }

    if (framedraw->ystart_param->dp==0) {
      ystartf=(double)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON (framedraw->ystart_param->widgets[0]));
      ystartf=ystartf/(double)cfile->vsize*(double)height;
    }
    else {
      ystartf=gtk_spin_button_get_value (GTK_SPIN_BUTTON (framedraw->ystart_param->widgets[0]));
      ystartf=ystartf*(double)height;
    }

    if (framedraw->yend_param->dp==0) {
      yendf=(double)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON (framedraw->yend_param->widgets[0]));
      yendf=yendf/(double)cfile->vsize*(double)height;
    }
    else {
      yendf=gtk_spin_button_get_value (GTK_SPIN_BUTTON (framedraw->yend_param->widgets[0]));
      yendf=yendf*(double)height;
    }

    if (b1_held||framedraw->type==LIVES_PARAM_SPECIAL_TYPE_RECT_MULTIRECT) {
      lives_painter_set_source_rgb(cr, 1., 0., 0.);
      lives_painter_rectangle(cr,xstartf-1.,ystartf-1.,xendf-xstartf+2.,yendf-ystartf+2.);
      lives_painter_stroke (cr);
    }
    else {
      if (!b1_held) {
	// create a mask which is only opaque within the clipping area

	lives_painter_rectangle(cr,0,0,width,height);
	lives_painter_rectangle(cr,xstartf,ystartf,xendf-xstartf+1.,yendf-ystartf+1.);
	lives_painter_set_operator(cr, LIVES_PAINTER_OPERATOR_DEST_OUT);
	lives_painter_set_source_rgba(cr, .0, .0, .0, .5);
	lives_painter_set_fill_rule(cr, LIVES_PAINTER_FILL_RULE_EVEN_ODD);
	lives_painter_fill (cr);
      }
    }

    break;
  case LIVES_PARAM_SPECIAL_TYPE_SINGLEPOINT:

    if (framedraw->xstart_param->dp==0) {
      xstartf=(double)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON (framedraw->xstart_param->widgets[0]));
      xstartf=xstartf/(double)cfile->hsize*(double)width;
    }
    else {
      xstartf=gtk_spin_button_get_value (GTK_SPIN_BUTTON (framedraw->xstart_param->widgets[0]));
      xstartf=xstartf*(double)width;
    }

    if (framedraw->ystart_param->dp==0) {
      ystartf=(double)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON (framedraw->ystart_param->widgets[0]));
      ystartf=ystartf/(double)cfile->vsize*(double)height;
    }
    else {
      ystartf=gtk_spin_button_get_value (GTK_SPIN_BUTTON (framedraw->ystart_param->widgets[0]));
      ystartf=ystartf*(double)height;
    }

    lives_painter_set_source_rgb(cr, 1., 0., 0.);

    lives_painter_move_to(cr,xstartf,ystartf-3);
    lives_painter_line_to(cr,xstartf,ystartf+3);

    lives_painter_stroke (cr);

    lives_painter_move_to(cr,xstartf-3,ystartf);
    lives_painter_line_to(cr,xstartf+3,ystartf);

    lives_painter_stroke (cr);

    break;

  default:

    break;

  }

  lives_painter_to_layer(cr, mainw->fd_layer);

  lives_painter_destroy(cr);

  redraw_framedraw_image ();
}



void load_rfx_preview(lives_rfx_t *rfx) {
  // load a preview of an rfx (rendered effect) in clip editor

  LiVESPixbuf *pixbuf;
  FILE *infofile=NULL;

  int max_frame=0,tot_frames=0;
  int vend=cfile->start;
  int retval;
  int alarm_handle;
  int current_file=mainw->current_file;

  boolean retb;
  boolean timeout;

  weed_timecode_t tc;
  const char *img_ext;

  if (mainw->framedraw_frame==0) mainw->framedraw_frame=1;

  lives_set_cursor_style(LIVES_CURSOR_BUSY,NULL);

  clear_mainw_msg();
  mainw->write_failed=FALSE;


#define LIVES_RFX_TIMER 10*U_SEC

  if (cfile->clip_type==CLIP_TYPE_FILE&&vend<=cfile->end) {
    // pull some frames for 10 seconds
    alarm_handle=lives_alarm_set(LIVES_RFX_TIMER);
    do {
      while (g_main_context_iteration(NULL,FALSE));
      if (is_virtual_frame(mainw->current_file,vend)) {
	retb=virtual_to_images(mainw->current_file,vend,vend,FALSE);
	if (!retb) return;
      }
      vend++;
      timeout=lives_alarm_get(alarm_handle);
    } while (vend<=cfile->end&&!timeout&&!mainw->cancelled);
    lives_alarm_clear(alarm_handle);
  }

  if (mainw->cancelled) {
    lives_set_cursor_style(LIVES_CURSOR_NORMAL,NULL);
    return;
  }

  // get message from back end processor
  while (!(infofile=fopen(cfile->info_file,"r"))&&!mainw->cancelled) {
    // wait until we get at least 1 frame
    while (g_main_context_iteration(NULL,FALSE));
    if (cfile->clip_type==CLIP_TYPE_FILE&&vend<=cfile->end) {
      // if we have a virtual clip (frames inside a video file)
      // pull some more frames to images to get us started
      do {
	retb=FALSE;
	if (is_virtual_frame(mainw->current_file,vend)) {
	  retb=virtual_to_images(mainw->current_file,vend,vend,FALSE);
	  if (!retb) {
	    fclose(infofile);
	    return;
	  }
	}
	vend++;
      } while (vend<=cfile->end&&!retb);
    }
    else {
      // otherwise wait
      g_usleep(prefs->sleep_time);
    }
  }

  if (mainw->cancelled) {
    if (infofile) fclose(infofile);
    lives_set_cursor_style(LIVES_CURSOR_NORMAL,NULL);
    return;
  }

  do {
    retval=0;
    mainw->read_failed=FALSE;
    lives_fgets(mainw->msg,512,infofile);
    if (mainw->read_failed) retval=do_read_failed_error_s_with_retry(cfile->info_file,NULL,NULL);
  } while (retval==LIVES_RETRY);

  fclose(infofile);


  if (strncmp(mainw->msg,"completed",9)) {
    if (rfx->num_in_channels>0) {
      max_frame=atoi(mainw->msg);
    }
    else {
      int numtok=get_token_count (mainw->msg,'|');
      if (numtok>4) {
	gchar **array=g_strsplit(mainw->msg,"|",numtok);
	max_frame=atoi(array[0]);
	cfile->hsize=atoi(array[1]);
	cfile->vsize=atoi(array[2]);
	cfile->fps=cfile->pb_fps=strtod(array[3],NULL);
	if (cfile->fps==0) cfile->fps=cfile->pb_fps=prefs->default_fps;
	tot_frames=atoi(array[4]);
	g_strfreev(array);
      }
    }
  }
  else {
    max_frame=cfile->end;
  }

  lives_set_cursor_style(LIVES_CURSOR_NORMAL,NULL);

  if (max_frame>0) {
    if (rfx->num_in_channels==0) {
      int maxlen=calc_spin_button_width(1.,(double)tot_frames,0);
      gtk_spin_button_set_range (GTK_SPIN_BUTTON (mainw->framedraw_spinbutton),1,tot_frames);
      gtk_entry_set_width_chars (GTK_ENTRY (mainw->framedraw_spinbutton),maxlen);
      gtk_widget_queue_draw(mainw->framedraw_spinbutton);
      gtk_widget_queue_draw(mainw->framedraw_scale);
    }
    
    if (mainw->framedraw_frame>max_frame) {
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(mainw->framedraw_spinbutton),max_frame);
      mainw->current_file=current_file;
      return;
    }
  }

  if (rfx->num_in_channels>0) {
    img_ext="pre";
  }
  else {
    img_ext=get_image_ext_for_type(cfile->img_type);
  }

  tc=((mainw->framedraw_frame-1.))/cfile->fps*U_SECL;
  pixbuf=pull_lives_pixbuf_at_size(mainw->current_file,mainw->framedraw_frame,(gchar *)img_ext,
				   tc,(double)cfile->hsize,
				   (double)cfile->vsize,LIVES_INTERP_BEST);


  load_framedraw_image(pixbuf);
  redraw_framedraw_image ();

  mainw->current_file=current_file;
}



void after_framedraw_frame_spinbutton_changed (GtkSpinButton *spinbutton, lives_special_framedraw_rect_t *framedraw) {
  // update the single frame/framedraw preview
  // after the "frame number" spinbutton has changed
  mainw->framedraw_frame=gtk_spin_button_get_value_as_int(spinbutton);
  if (lives_widget_is_visible(mainw->framedraw_preview)) {
    if (mainw->framedraw_preview!=NULL) gtk_widget_set_sensitive(mainw->framedraw_preview,FALSE);
    while (g_main_context_iteration(NULL,FALSE));
    load_rfx_preview(framedraw->rfx);
  }
  else framedraw_redraw(framedraw, TRUE, NULL);
}




void load_framedraw_image(LiVESPixbuf *pixbuf) {
  // this is for the single frame framedraw widget
  // it should be called whenever mainw->framedraw_bitmap changes

  weed_timecode_t tc;

  if (mainw->framedraw_frame>cfile->frames) mainw->framedraw_frame=cfile->frames;

  if (pixbuf==NULL) {
    const gchar *img_ext=get_image_ext_for_type(cfile->img_type);

    // can happen if we preview for rendered generators
    if ((mainw->multitrack==NULL||mainw->current_file!=mainw->multitrack->render_file)&&mainw->framedraw_frame==0) return;

    tc=((mainw->framedraw_frame-1.))/cfile->fps*U_SECL;
    pixbuf=pull_lives_pixbuf_at_size(mainw->current_file,mainw->framedraw_frame,img_ext,tc,
				     (double)cfile->hsize,(double)cfile->vsize,
				     LIVES_INTERP_BEST);
  }

  if (pixbuf!=NULL) {
    if (mainw->fd_layer_orig!=NULL) {
      weed_layer_free(mainw->fd_layer_orig);
    }

    mainw->fd_layer_orig=weed_layer_new(0,0,NULL,WEED_PALETTE_END);

    if (pixbuf_to_layer(mainw->fd_layer_orig,pixbuf)) {
      mainw->do_not_free=(gpointer)lives_pixbuf_get_pixels_readonly(pixbuf);
      mainw->free_fn=lives_free_with_check;
    }
    g_object_unref(pixbuf);
    mainw->do_not_free=NULL;
    mainw->free_fn=lives_free_normal;

  }

  if (mainw->fd_layer!=NULL) weed_layer_free(mainw->fd_layer);
  mainw->fd_layer=NULL;

}





void redraw_framedraw_image(void) {
  lives_painter_t *cr;
  LiVESPixbuf *pixbuf;

  int fd_width=lives_widget_get_allocation_width(mainw->framedraw);
  int fd_height=lives_widget_get_allocation_height(mainw->framedraw);

  int width,height;


  if (mainw->fd_layer_orig==NULL) return;

  if (mainw->current_file<1||cfile==NULL) return;

  width=cfile->hsize;
  height=cfile->vsize;

  calc_maxspect(fd_width,fd_height,&width,&height);

  // copy orig layer to layer
  if (mainw->fd_layer==NULL) mainw->fd_layer=weed_layer_copy(NULL,mainw->fd_layer_orig);

  // force to RGB24
  convert_layer_palette(mainw->fd_layer,WEED_PALETTE_RGBA32,0);

  // resize to correct size
  resize_layer(mainw->fd_layer, width, height, LIVES_INTERP_BEST);

  // layer to pixbuf
  pixbuf=layer_to_pixbuf(mainw->fd_layer);

  // get lives_painter for window
  cr = lives_painter_create_from_widget (mainw->framedraw);

  if (cr!=NULL) {
    // set source pixbuf for lives_painter
    lives_painter_set_source_pixbuf (cr, pixbuf, (fd_width-width)>>1, (fd_height-height)>>1);
    lives_painter_paint (cr);
    lives_painter_destroy(cr);
  }

  // convert pixbuf back to layer (layer_to_pixbuf destroys it)
  if (pixbuf_to_layer(mainw->fd_layer,pixbuf)) {
    mainw->do_not_free=(gpointer)lives_pixbuf_get_pixels_readonly(pixbuf);
    mainw->free_fn=lives_free_with_check;
  }

  g_object_unref(pixbuf);
  mainw->do_not_free=NULL;
  mainw->free_fn=lives_free_normal;


}


// change cursor maybe when we enter or leave the framedraw window

boolean on_framedraw_enter (GtkWidget *widget, GdkEventCrossing *event, lives_special_framedraw_rect_t *framedraw) {
  GdkCursor *cursor;

  if (framedraw==NULL&&mainw->multitrack!=NULL) {
    framedraw=mainw->multitrack->framedraw;
    if (framedraw==NULL&&mainw->multitrack->cursor_style==0) gdk_window_set_cursor 
							       (lives_widget_get_xwindow(mainw->multitrack->play_box), NULL);
  }

  if (framedraw==NULL) return FALSE;
  if (mainw->multitrack!=NULL&&(mainw->multitrack->track_index==-1||mainw->multitrack->cursor_style!=0)) return FALSE;

  switch (framedraw->type) {
  case LIVES_PARAM_SPECIAL_TYPE_RECT_DEMASK:
  case LIVES_PARAM_SPECIAL_TYPE_RECT_MULTRECT:
  case LIVES_PARAM_SPECIAL_TYPE_RECT_MULTIRECT:
    if (mainw->multitrack==NULL) {
      cursor=gdk_cursor_new_for_display 
	(mainw->mgeom[prefs->gui_monitor>0?prefs->gui_monitor-1:0].disp,
	 GDK_TOP_LEFT_CORNER);
      gdk_window_set_cursor (lives_widget_get_xwindow(mainw->framedraw), cursor);
    }
    else {
      cursor=gdk_cursor_new_for_display 
	(mainw->mgeom[prefs->gui_monitor>0?prefs->gui_monitor-1:0].disp,
	 GDK_TOP_LEFT_CORNER);
      gdk_window_set_cursor (lives_widget_get_xwindow(mainw->multitrack->play_box), cursor);
    }
    break;
  case LIVES_PARAM_SPECIAL_TYPE_SINGLEPOINT:
    if (mainw->multitrack==NULL) {
      cursor=gdk_cursor_new_for_display 
	(mainw->mgeom[prefs->gui_monitor>0?prefs->gui_monitor-1:0].disp,
	 GDK_CROSSHAIR);
      gdk_window_set_cursor (lives_widget_get_xwindow(mainw->framedraw), cursor);
    }
    else {
      cursor=gdk_cursor_new_for_display (mainw->multitrack->display, GDK_CROSSHAIR);
      gdk_window_set_cursor (lives_widget_get_xwindow(mainw->multitrack->play_box), cursor);
    }
    break;

  default:
    break;

  }
  return FALSE;
}

boolean on_framedraw_leave (GtkWidget *widget, GdkEventCrossing *event, lives_special_framedraw_rect_t *framedraw) {
  if (framedraw==NULL) return FALSE;
  gdk_window_set_cursor (lives_widget_get_xwindow(mainw->framedraw), NULL);
  return FALSE;
}


// using these 3 functions, the user can draw on frames

boolean on_framedraw_mouse_start (GtkWidget *widget, GdkEventButton *event, lives_special_framedraw_rect_t *framedraw) {
  // user clicked in the framedraw widget (or multitrack playback widget)

  int fd_height;
  int fd_width;

  int width=cfile->hsize;
  int height=cfile->vsize;

  int xstarti,ystarti;

  if (framedraw==NULL&&mainw->multitrack!=NULL) framedraw=mainw->multitrack->framedraw;

  if (framedraw==NULL) return FALSE;
  if (mainw->multitrack!=NULL&&mainw->multitrack->track_index==-1) return FALSE;

  if (framedraw==NULL&&mainw->multitrack!=NULL&&event->button==3) {
    // right click brings up context menu
    frame_context(widget,event,GINT_TO_POINTER(0));
  }

  if (event->button!=1) return FALSE;

  b1_held=TRUE;

  lives_widget_get_pointer((LiVESXDevice *)mainw->mgeom[prefs->gui_monitor>0?prefs->gui_monitor-1:0].mouse_device, 
			   widget, &xstarti, &ystarti);

  if ((framedraw->type==LIVES_PARAM_SPECIAL_TYPE_RECT_MULTRECT||
       framedraw->type==LIVES_PARAM_SPECIAL_TYPE_RECT_MULTIRECT||
       framedraw->type==LIVES_PARAM_SPECIAL_TYPE_RECT_DEMASK)&&
      (mainw->multitrack==NULL||mainw->multitrack->cursor_style==0)) {
    GdkCursor *cursor;
    if (mainw->multitrack==NULL) {
      cursor=gdk_cursor_new_for_display 
	(mainw->mgeom[prefs->gui_monitor>0?prefs->gui_monitor-1:0].disp, 
	 GDK_BOTTOM_RIGHT_CORNER);
      gdk_window_set_cursor (lives_widget_get_xwindow(widget), cursor);
    }
    else {
      cursor=gdk_cursor_new_for_display (mainw->multitrack->display, GDK_BOTTOM_RIGHT_CORNER);
      gdk_window_set_cursor (lives_widget_get_xwindow(mainw->multitrack->play_box), cursor);
    }
  }


  fd_width=lives_widget_get_allocation_width(widget);
  fd_height=lives_widget_get_allocation_height(widget);
    
  calc_maxspect(fd_width,fd_height,&width,&height);

  xstart=(double)xstarti-(double)(fd_width-width)/2.;
  ystart=(double)ystarti-(double)(fd_height-height)/2.;

  xstart/=(double)width;
  ystart/=(double)height;

  xcurrent=xstart;
  ycurrent=ystart;

  noupdate=TRUE;

  switch (framedraw->type) {
  case LIVES_PARAM_SPECIAL_TYPE_SINGLEPOINT: 

    if (framedraw->xstart_param->dp>0)
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xstart_param->widgets[0]),xstart);
    else
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xstart_param->widgets[0]),(int)(xstart*(double)cfile->hsize+.5));
    if (framedraw->xstart_param->dp>0)
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->ystart_param->widgets[0]),ystart);
    else
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->ystart_param->widgets[0]),(int)(ystart*(double)cfile->vsize+.5));

    break;

  case LIVES_PARAM_SPECIAL_TYPE_RECT_MULTRECT: 
  case LIVES_PARAM_SPECIAL_TYPE_RECT_MULTIRECT: 
  case LIVES_PARAM_SPECIAL_TYPE_RECT_DEMASK: 

    if (framedraw->xstart_param->dp>0)
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xstart_param->widgets[0]),xstart);
    else
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xstart_param->widgets[0]),(int)(xstart*(double)cfile->hsize+.5));
    if (framedraw->xstart_param->dp>0)
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->ystart_param->widgets[0]),ystart);
    else
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->ystart_param->widgets[0]),(int)(ystart*(double)cfile->vsize+.5));

    if (framedraw->type==LIVES_PARAM_SPECIAL_TYPE_RECT_MULTRECT) {
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xend_param->widgets[0]),0.);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->yend_param->widgets[0]),0.);
    }
    else {
      if (framedraw->xend_param->dp>0)
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xend_param->widgets[0]),xstart);
      else
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xend_param->widgets[0]),(int)(xstart*(double)cfile->hsize+.5));
      if (framedraw->xend_param->dp>0)
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->yend_param->widgets[0]),ystart);
      else
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->yend_param->widgets[0]),(int)(ystart*(double)cfile->vsize+.5));
    }


    break;
  default:
    break;
  }


  if (mainw->framedraw_reset!=NULL) {
    gtk_widget_set_sensitive (mainw->framedraw_reset,TRUE);
  }
  if (mainw->framedraw_preview!=NULL) {
    gtk_widget_set_sensitive (mainw->framedraw_preview,TRUE);
  }

  noupdate=FALSE;

  framedraw_redraw (framedraw, FALSE, NULL);

  return FALSE;
}

boolean on_framedraw_mouse_update (GtkWidget *widget, GdkEventMotion *event, lives_special_framedraw_rect_t *framedraw) {
  // pointer moved in the framedraw widget
  int xcurrenti,ycurrenti;

  int fd_width,fd_height,width,height;

  if (noupdate) return FALSE;

  if (!b1_held) return FALSE;

  if (framedraw==NULL&&mainw->multitrack!=NULL) framedraw=mainw->multitrack->framedraw;
  if (framedraw==NULL) return FALSE;
  if (mainw->multitrack!=NULL&&mainw->multitrack->track_index==-1) return FALSE;

  lives_widget_get_pointer((LiVESXDevice *)mainw->mgeom[prefs->gui_monitor>0?prefs->gui_monitor-1:0].mouse_device, 
			   widget, &xcurrenti, &ycurrenti);


  width=cfile->hsize;
  height=cfile->vsize;

  fd_width=lives_widget_get_allocation_width(widget);
  fd_height=lives_widget_get_allocation_height(widget);
      
  calc_maxspect(fd_width,fd_height,&width,&height);

  xcurrent=(double)xcurrenti-(fd_width-width)/2.;
  ycurrent=(double)ycurrenti-(fd_height-height)/2.;

  xcurrent/=(double)width;
  ycurrent/=(double)height;

  noupdate=TRUE;


  switch (framedraw->type) {
  case LIVES_PARAM_SPECIAL_TYPE_RECT_MULTRECT:
    {
      double xscale,yscale;

      xscale=xcurrent-xstart;
      yscale=ycurrent-ystart;
      
      if (xscale>0.) {
	if (framedraw->xend_param->dp>0)
	  gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xend_param->widgets[0]),xscale);
	else
	  gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xend_param->widgets[0]),(int)(xscale*(double)cfile->hsize+.5));
      }
      else {
	if (framedraw->xstart_param->dp>0) {
	  gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xend_param->widgets[0]),-xscale);
	  gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xstart_param->widgets[0]),xcurrent);
	}
	else {
	  gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xend_param->widgets[0]),(int)(-xscale*(double)cfile->hsize-.5));
	  gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xstart_param->widgets[0]),(int)(xcurrent*(double)cfile->hsize+.5));
	}
      }

      if (yscale>0.) {
	if (framedraw->yend_param->dp>0)
	  gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->yend_param->widgets[0]),yscale);
	else 
	  gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->yend_param->widgets[0]),(int)(yscale*(double)cfile->vsize+.5));
      }
      else {
	if (framedraw->xstart_param->dp>0) {
	  gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->yend_param->widgets[0]),-yscale);
	  gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->ystart_param->widgets[0]),ycurrent);
	}
	else {
	  gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->yend_param->widgets[0]),(int)(-yscale*(double)cfile->vsize-.5));
	  gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->ystart_param->widgets[0]),(int)(ycurrent*(double)cfile->vsize+.5));
	}
      }

    }
    break;
  case LIVES_PARAM_SPECIAL_TYPE_RECT_MULTIRECT:
  case LIVES_PARAM_SPECIAL_TYPE_RECT_DEMASK:

    if (xcurrent>xstart) {
      if (framedraw->xend_param->dp>0)
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xend_param->widgets[0]),xcurrent);
      else
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xend_param->widgets[0]),(int)(xcurrent*(double)cfile->hsize+.5));
    }
    else {
      if (framedraw->xstart_param->dp>0) {
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xend_param->widgets[0]),xstart);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xstart_param->widgets[0]),xcurrent);
      }
      else {
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xend_param->widgets[0]),(int)(xstart*(double)cfile->hsize+.5));
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xstart_param->widgets[0]),(int)(xcurrent*(double)cfile->hsize+.5));
      }
    }

    if (ycurrent>ystart) {
      if (framedraw->yend_param->dp>0)
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->yend_param->widgets[0]),ycurrent);
      else 
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->yend_param->widgets[0]),(int)(ycurrent*(double)cfile->vsize+.5));
    }
    else {
      if (framedraw->xstart_param->dp>0) {
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->yend_param->widgets[0]),ystart);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->ystart_param->widgets[0]),ycurrent);
      }
      else {
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->yend_param->widgets[0]),(int)(ystart*(double)cfile->vsize+.5));
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->ystart_param->widgets[0]),(int)(ycurrent*(double)cfile->vsize+.5));
      }
    }


    break;

  default:
    break;

  }

  if (mainw->framedraw_reset!=NULL) {
    gtk_widget_set_sensitive (mainw->framedraw_reset,TRUE);
  }
  if (mainw->framedraw_preview!=NULL) {
    gtk_widget_set_sensitive (mainw->framedraw_preview,TRUE);
  }

  noupdate=FALSE;
  framedraw_redraw (framedraw, FALSE, NULL);

  return FALSE;
}


boolean on_framedraw_mouse_reset (GtkWidget *widget, GdkEventButton *event, lives_special_framedraw_rect_t *framedraw) {
  // user released the mouse button in framedraw widget
  if (event->button!=1||!b1_held) return FALSE;

  b1_held=FALSE;

  if (framedraw==NULL&&mainw->multitrack!=NULL) framedraw=mainw->multitrack->framedraw;
  if (framedraw==NULL) return FALSE;
  if (mainw->multitrack!=NULL&&mainw->multitrack->track_index==-1) return FALSE;

  framedraw_redraw(framedraw, FALSE, NULL);
  return FALSE;
}


void after_framedraw_widget_changed (GtkWidget *widget, lives_special_framedraw_rect_t *framedraw) {
  if (mainw->block_param_updates||noupdate) return;

  // redraw mask when spin values change
  framedraw_redraw (framedraw, FALSE, NULL);
  if (mainw->framedraw_reset!=NULL) {
    gtk_widget_set_sensitive (mainw->framedraw_reset,TRUE);
  }
  if (mainw->framedraw_preview!=NULL) {
    gtk_widget_set_sensitive (mainw->framedraw_preview,TRUE);
  }
}



void on_framedraw_reset_clicked (GtkButton *button, lives_special_framedraw_rect_t *framedraw) {
  // reset to defaults

  noupdate=TRUE;
  if (framedraw->xend_param!=NULL) {
    if (framedraw->xend_param->dp==0)
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xend_param->widgets[0]),(double)get_int_param(framedraw->xend_param->def));
    else 
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xend_param->widgets[0]),get_double_param(framedraw->xend_param->def));
  }
  if (framedraw->yend_param!=NULL) {
    if (framedraw->yend_param->dp==0)
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->yend_param->widgets[0]),(double)get_int_param(framedraw->yend_param->def));
    else 
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->yend_param->widgets[0]),get_double_param(framedraw->yend_param->def));
  }
  if (framedraw->xstart_param!=NULL) {
    if (framedraw->xstart_param->dp==0)
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xstart_param->widgets[0]),(double)get_int_param(framedraw->xstart_param->def));
    else 
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->xstart_param->widgets[0]),get_double_param(framedraw->xstart_param->def));
  }
  if (framedraw->ystart_param!=NULL) {
    if (framedraw->ystart_param->dp==0)
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->ystart_param->widgets[0]),(double)get_int_param(framedraw->ystart_param->def));
    else 
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(framedraw->ystart_param->widgets[0]),get_double_param(framedraw->ystart_param->def));
  }

  if (mainw->framedraw_reset!=NULL) {
    gtk_widget_set_sensitive (mainw->framedraw_reset,TRUE);
  }
  if (mainw->framedraw_preview!=NULL) {
    gtk_widget_set_sensitive (mainw->framedraw_preview,TRUE);
  }

  // update widgets now
  while (g_main_context_iteration(NULL,FALSE));

  noupdate=FALSE;

  framedraw_redraw (framedraw, FALSE, NULL);

}

// utils.c
// LiVES
// (c) G. Finch 2003 - 2012 <salsaman@gmail.com>
// released under the GNU GPL 3 or later
// see file ../COPYING or www.gnu.org for licensing details


#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef IS_MINGW
#include <sys/statvfs.h>
#endif
#include <sys/file.h>

#include "main.h"
#include "support.h"
#include "audio.h"
#include "resample.h"

static boolean  omute,  osepwin,  ofs,  ofaded,  odouble;


typedef struct {
  int fd;
  size_t bytes;
  boolean eof;
  uint8_t *ptr;
  uint8_t *buffer;
  boolean read;
  boolean allow_fail;
} lives_file_buffer_t;


char *filename_from_fd(char *val, int fd) {
  // return filename from an open fd, freeing val first

  // in case of error we return val


  // call like: foo=filename_from_fd(foo,fd);

#ifndef IS_MINGW
  char *fdpath;
  char *fidi;
  char rfdpath[PATH_MAX];
  struct stat stb0,stb1;

  ssize_t slen;

  if (fstat(fd,&stb0)) return val;

  fidi=g_strdup_printf("%d",fd);
  fdpath=g_build_filename("/proc","self","fd",fidi,NULL);
  g_free(fidi);

  if ((slen=lives_readlink(fdpath,rfdpath,PATH_MAX))==-1) return val;
  g_free(fdpath);

  memset(rfdpath+slen,0,1);

  if (stat(rfdpath,&stb1)) return val;

  if (stb0.st_dev!=stb1.st_dev) return val;

  if (stb0.st_ino!=stb1.st_ino) return val;

  if (val!=NULL) g_free(val);
  return g_strdup(rfdpath);
#else
  return g_strdup("unknown");
#endif
}



static void reverse_bytes(uint8_t *out, const uint8_t *in, size_t count) {
  register int i;
  for (i=0;i<count;i++) {
    out[i]=in[count-i-1];
  }
}



// system calls

LIVES_INLINE void lives_srandom(unsigned int seed) {
#ifdef IS_MINGW
  srand(seed);
#else
  srandom(seed);
#endif
}


LIVES_INLINE uint64_t lives_random(void) {
#ifdef IS_MINGW
  return (uint64_t)rand()*(uint64_t)fastrand();
#else
  return random();
#endif
}



LIVES_INLINE pid_t lives_getpid(void) {
#ifdef IS_MINGW
  return _getpid();
#else
  return getpid();
#endif
}


#ifdef IS_MINGW
LIVES_INLINE int sidhash(char *strsid) {
  size_t slen;
  int retval=0;
  register int i;

  if (strsid==NULL) return 0;

  slen=strlen(strsid);

  g_print("\n\nval is %d %c\n",slen,strsid[0]);

  for (i=4;i<slen;i++) {
    retval+=(uint8_t)strsid[i];
  }

  g_print("\n\ngot token value %d\n",retval);
  return retval;
}
#endif





LIVES_INLINE int lives_getuid(void) {
#ifdef IS_MINGW
  HANDLE Thandle;
  DWORD DAmask, size;
  char *strsid;
  int retval=0;

  DWORD dwIndex;
  PTOKEN_GROUPS ptg = NULL;
  PSID psid=NULL;


  DAmask = TOKEN_READ;
  
  if (!OpenProcessToken(GetCurrentProcess(), DAmask, &Thandle)) {
    LIVES_ERROR("could not open token");
    return 0;
  }
  
  if (!GetTokenInformation(Thandle, TokenGroups, ptg, 0, &size)) {
    CloseHandle(Thandle);
    LIVES_ERROR("could not open token info");
    return 0;
  }

  ptg = (PTOKEN_GROUPS)HeapAlloc(GetProcessHeap(),
				 HEAP_ZERO_MEMORY, size);

  if (ptg == NULL)
    goto Cleanup;


// Loop through the groups to find the logon SID.

  for (dwIndex = 0; dwIndex < ptg->GroupCount; dwIndex++) {
    if ((ptg->Groups[dwIndex].Attributes & SE_GROUP_LOGON_ID)
	==  SE_GROUP_LOGON_ID) 
      {
	// Found the logon SID; make a copy of it.

	size = GetLengthSid(ptg->Groups[dwIndex].Sid);
	psid = (PSID) HeapAlloc(GetProcessHeap(),
				  HEAP_ZERO_MEMORY, size);
	if (psid == NULL)
	  goto Cleanup;
	if (!CopySid(size, psid, ptg->Groups[dwIndex].Sid)) 
	  {
	    HeapFree(GetProcessHeap(), 0, (LPVOID)psid);
	    goto Cleanup;
	  }
	break;
      }
  }

  if (psid!=NULL) {
    ConvertSidToStringSid(psid,&strsid);
    // string sid is eg: S-1-5-5-X-Y
    retval=sidhash(strsid);
    LocalFree(strsid);
  }

 Cleanup:

   if (ptg != NULL)
      HeapFree(GetProcessHeap(), 0, (LPVOID)ptg);

   //CloseHandle(Thandle);
  return retval;
#else
  return geteuid();
#endif
  return 0; // stop gcc complaining
}


LIVES_INLINE int lives_getgid(void) {
#ifdef IS_MINGW
  return lives_getuid();
#else
  return getegid();
#endif
  return 0; // stop gcc complaining
}



LIVES_INLINE ssize_t lives_readlink(const char *path, char *buf, size_t bufsiz) {
#ifdef IS_MINGW
  ssize_t sz=strlen(path);
  if (sz>bufsiz) sz=bufsiz;
  memcpy(buf,path,sz);
  return sz;
#else
  return readlink(path,buf,bufsiz);
#endif
}


LIVES_INLINE boolean lives_fsync(int fd) {
  // ret TRUE on success
#ifndef IS_MINGW
  return !fsync(fd);
#else
  return _commit(fd);
#endif
}


LIVES_INLINE void lives_sync(void) {
  // ret TRUE on success
#ifndef IS_MINGW
  sync();
#else
  system("sync.exe");
  return;
#endif
}


LIVES_INLINE boolean lives_setenv(const char *name, const char *value) {
  // ret TRUE on success
#if IS_MINGW
  return SetEnvironmentVariable(name,value);
#else
#if IS_IRIX
  int len  = strlen(name) + strlen(value) + 2;
  char *env = malloc(len);
  if (env != NULL) {
    strcpy(env, name);
    strcat(env, "=");
    strcat(env, val);
    return !putenv(env);
  }
}
#else
  return !setenv(name,value,1);
#endif
#endif
}



int lives_system(const char *com, boolean allow_error) {
  int retval;
  boolean cnorm=FALSE;

  // TODO - use g_spawn ?

  if (mainw->is_ready&&!mainw->is_exiting&&
      ((mainw->multitrack==NULL&&mainw->cursor_style==LIVES_CURSOR_NORMAL)||
       (mainw->multitrack!=NULL&&mainw->multitrack->cursor_style==LIVES_CURSOR_NORMAL))) {
    cnorm=TRUE;
    lives_set_cursor_style(LIVES_CURSOR_BUSY,NULL);

    lives_widget_context_update();
  }

  retval=system(com);

  if (retval
#ifdef IS_MINGW
      &&retval!=9009
#endif
      ) {
    char *msg=NULL;
    mainw->com_failed=TRUE;
    if (!allow_error) {
      msg=g_strdup_printf("lives_system failed with code %d: %s",retval,com);
      LIVES_ERROR(msg);
      do_system_failed_error(com,retval,NULL);
    }
#ifndef LIVES_NO_DEBUG
    else {
      msg=g_strdup_printf("lives_system failed with code %d: %s (not an error)",retval,com);
      LIVES_DEBUG(msg);
    }
#endif
    if (msg!=NULL) g_free(msg);
  }

  if (cnorm) lives_set_cursor_style(LIVES_CURSOR_NORMAL,NULL);

  return retval;
}



lives_pid_t lives_fork(const char *com) {
  // returns a negative number which is the pgid to lives_kill

  // mingw - return PROCESS_INFORMATION * to use in GenerateConsoleCtrlEvent (?)

  // to signal to sub process and all children
  // TODO *** - error check

#ifndef IS_MINGW
  pid_t ret;

  if (!(ret=fork())) {
    int dummy;
    setsid(); // create new session id
    setpgid(getpid(),0); // create new pgid
    dummy=system(com);
    dummy=dummy;
    _exit(0);
  }

  return ret;
#else
  STARTUPINFO si;
  PROCESS_INFORMATION *pi=malloc(sizeof(PROCESS_INFORMATION));

  CreateProcess(NULL,(char *)com,NULL,NULL,FALSE,CREATE_NEW_PROCESS_GROUP,NULL,NULL,&si,pi);
  return pi;
#endif
}





ssize_t lives_write(int fd, const void *buf, size_t count, boolean allow_fail) {
  ssize_t retval;
  retval=write(fd, buf, count);

  if (retval<count) {
    char *msg=NULL;
    mainw->write_failed=TRUE;
    mainw->write_failed_file=filename_from_fd(mainw->write_failed_file,fd);
    if (retval>=0)
      msg=g_strdup_printf("Write failed %"PRIu64" of %"PRIu64" in: %s",(uint64_t)retval,
			  (uint64_t)count,mainw->write_failed_file);
    else 
      msg=g_strdup_printf("Write failed with error %"PRIu64" in: %s",(uint64_t)retval,
			  mainw->write_failed_file);

    if (!allow_fail) {
      LIVES_ERROR(msg);
      close(fd);
    }
#ifndef LIVES_NO_DEBUG
    else {
      char *ffile=filename_from_fd(NULL,fd);
      if (retval>=0)
	msg=g_strdup_printf("Write failed %"PRIu64" of %"PRIu64" in: %s (not an error)",(uint64_t)retval,
			    (uint64_t)count,ffile);
      else 
	msg=g_strdup_printf("Write failed with error %"PRIu64" in: %s (allowed)",(uint64_t)retval,
			    mainw->write_failed_file);
      LIVES_DEBUG(msg);
      g_free(ffile);
    }
#endif
    if (msg!=NULL) g_free(msg);
  }
  return retval;
}




ssize_t lives_write_le(int fd, const void *buf, size_t count, boolean allow_fail) {
  if (capable->byte_order==LIVES_BIG_ENDIAN&&(prefs->bigendbug!=1)) {
    uint8_t xbuf[count];
    reverse_bytes(xbuf,(const uint8_t *)buf,count);
    return lives_write(fd,xbuf,count,allow_fail);
  }
  else {
    return lives_write(fd,buf,count,allow_fail);
  }

}



int lives_fputs(const char *s, FILE *stream) {
  int retval;

  retval=fputs(s,stream);

  if (retval==EOF) {
    mainw->write_failed=TRUE;
  }

  return retval;
}


char *lives_fgets(char *s, int size, FILE *stream) {
  char *retval;

  retval=fgets(s,size,stream);

  if (retval==NULL&&ferror(stream)) {
    mainw->read_failed=TRUE;
  }

  return retval;
}


static lives_file_buffer_t *find_in_file_buffers(int fd) {
  lives_file_buffer_t *fbuff;
  GList *fblist=mainw->file_buffers;

  while (fblist!=NULL) {
    fbuff=(lives_file_buffer_t *)fblist->data;
    if (fbuff->fd==fd) return fbuff;
    fblist=fblist->next;
  }

  return NULL;
}

void lives_close_all_file_buffers(void) {
  lives_file_buffer_t *fbuff;

  while (mainw->file_buffers!=NULL) {
    fbuff=(lives_file_buffer_t *)mainw->file_buffers->data;
    lives_close_buffered(fbuff->fd);
  }

}


static void do_file_read_error(int fd, ssize_t errval, size_t count) {
  char *msg=NULL;
  mainw->read_failed=TRUE;
  mainw->read_failed_file=filename_from_fd(mainw->read_failed_file,fd);
 
  if (errval>=0)
    msg=g_strdup_printf("Read failed %"PRId64" of %"PRIu64" in: %s",(int64_t)errval,
			(uint64_t)count,mainw->read_failed_file);
  else 
    msg=g_strdup_printf("Read failed with error %"PRId64" in: %s",(int64_t)errval,
			mainw->read_failed_file);



  LIVES_ERROR(msg);
  g_free(msg);
}


ssize_t lives_read(int fd, void *buf, size_t count, boolean allow_less) {
  ssize_t retval=read(fd, buf, count);

  if (retval<count) {
    if (!allow_less||retval<0) {
      do_file_read_error(fd,retval,count);
      close(fd);
    }
#ifndef LIVES_NO_DEBUG
    else {
      char *msg=NULL;
      char *ffile=filename_from_fd(NULL,fd);
      msg=g_strdup_printf("Read got %"PRIu64" of %"PRIu64" in: %s (not an error)",(uint64_t)retval,
			  (uint64_t)count,ffile);
      LIVES_DEBUG(msg);
      g_free(ffile); g_free(msg);
    }
#endif
  }
  return retval;
}



ssize_t lives_read_le(int fd, void *buf, size_t count, boolean allow_less) {
  if (capable->byte_order==LIVES_BIG_ENDIAN&&!prefs->bigendbug) {
    uint8_t xbuf[count];
    ssize_t retval=lives_read(fd,buf,count,allow_less);
    if (retval<count) return retval;
    reverse_bytes((uint8_t *)buf,(const uint8_t *)xbuf,count);
    return retval;
  }
  else {
    return lives_read(fd,buf,count,allow_less);
  }

}




//// buffered io ////

#define BUFFER_FILL_BYTES 65536


static ssize_t file_buffer_flush(lives_file_buffer_t *fbuff) {
  ssize_t res;

  res=lives_write(fbuff->fd,fbuff->buffer,fbuff->bytes,fbuff->allow_fail);

  if (!fbuff->allow_fail&&res<fbuff->bytes) {
    lives_close_buffered(-fbuff->fd); // use -fd as lives_write will have closed
  }

  return res;
}


static int lives_open_real_buffered(const char *pathname, int flags, int mode, boolean isread) {
  lives_file_buffer_t *fbuff;
  int fd=open(pathname,flags,mode);
  if (fd>=0) {
    fbuff=(lives_file_buffer_t *)g_malloc(sizeof(lives_file_buffer_t));
    fbuff->fd=fd;
    fbuff->bytes=0;
    fbuff->eof=FALSE;
    fbuff->ptr=NULL;
    fbuff->buffer=NULL;
    fbuff->read=isread;
    mainw->file_buffers=g_list_append(mainw->file_buffers,(gpointer)fbuff);
  }

  return fd;
}


LIVES_INLINE int lives_open_buffered_rdonly(const char *pathname) {
  return lives_open_real_buffered(pathname,O_RDONLY,0,TRUE);
}


LIVES_INLINE int lives_creat_buffered(const char *pathname, int mode) {
  return lives_open_real_buffered(pathname,O_CREAT|O_WRONLY|O_TRUNC,mode,FALSE);
}


int lives_close_buffered(int fd) {
  lives_file_buffer_t *fbuff;
  boolean should_close=TRUE;
  int ret=0;

  if (fd<0) {
    should_close=FALSE;
    fd=-fd;
  }

  fbuff=find_in_file_buffers(fd);

  if (fbuff==NULL) {
    // normal non-buffered file
    LIVES_DEBUG("lives_close_buffered: no file buffer found");
    if (should_close) ret=close(fd);
    return ret;
  }

  if (!fbuff->read&&should_close) {
    boolean allow_fail=fbuff->allow_fail;
    size_t bytes=fbuff->bytes;

    ret=file_buffer_flush(fbuff);
    if (!allow_fail&&ret<bytes) return ret; // this is correct, as flush will have called close again with should_close=FALSE;
  }

  if (should_close && fbuff->fd>=0) ret=close(fbuff->fd);

  mainw->file_buffers=g_list_remove(mainw->file_buffers,(gconstpointer)fbuff);
  if (fbuff->buffer!=NULL) g_free(fbuff->buffer);
  g_free(fbuff);
  return ret;
}



static ssize_t file_buffer_fill(lives_file_buffer_t *fbuff) {
  ssize_t res;

  if (fbuff->buffer==NULL) fbuff->buffer=(uint8_t *)g_malloc(BUFFER_FILL_BYTES);

  res=lives_read(fbuff->fd,fbuff->buffer,BUFFER_FILL_BYTES,TRUE);

  if (res<0) {
    lives_close_buffered(-fbuff->fd); // use -fd as lives_read will have closed
    return res;
  }

  fbuff->bytes=res;
  fbuff->ptr=fbuff->buffer;

  if (res<BUFFER_FILL_BYTES) fbuff->eof=TRUE;
  else fbuff->eof=FALSE;

  return res;
}


off_t lives_lseek_buffered_rdonly(int fd, off_t offset) {
  // seek +/- offset from current

  lives_file_buffer_t *fbuff;

  if ((fbuff=find_in_file_buffers(fd))==NULL) {
    LIVES_DEBUG("lives_lseek_buffered_rdonly: no file buffer found");
    return lseek(fd,offset,SEEK_CUR);
  }

  fbuff->ptr+=offset;
  fbuff->bytes-=offset;

  if (fbuff->bytes<=0||fbuff->bytes>BUFFER_FILL_BYTES) {
    fbuff->bytes=0;
  }

  return lseek(fd,offset,SEEK_CUR);
}



ssize_t lives_read_buffered(int fd, void *buf, size_t count, boolean allow_less) {
  lives_file_buffer_t *fbuff;
  ssize_t retval=0,res;
  size_t ocount=count;
  uint8_t *ptr=(uint8_t *)buf;

  if ((fbuff=find_in_file_buffers(fd))==NULL) {
    LIVES_DEBUG("lives_read_buffered: no file buffer found");
    return lives_read(fd,buf,count,allow_less);
  }

  if (!fbuff->read) {
    LIVES_ERROR("lives_read_buffered: wrong buffer type");
    return 0;
  }

  // read bytes from fbuff 
  while (1) {
    if (fbuff->bytes==0&&!fbuff->eof) {
      res=file_buffer_fill(fbuff);
      if (res<0) return res;
    }

    if (fbuff->bytes<count) {
      lives_memcpy(ptr,fbuff->ptr,fbuff->bytes);
      retval+=fbuff->bytes;
      count-=fbuff->bytes;
      ptr+=fbuff->bytes;
      fbuff->bytes=0;
      if (fbuff->eof) {
	break;
      }
    }
    else {
      lives_memcpy(ptr,fbuff->ptr,count);
      retval+=count;
      fbuff->ptr+=count;
      fbuff->bytes-=count;
      count=0;
      break;
    }
  }

  if (!allow_less && count>0) {
    do_file_read_error(fd,retval,ocount);
    lives_close_buffered(fd);
  }

  return retval;
}


ssize_t lives_read_le_buffered(int fd, void *buf, size_t count, boolean allow_less) {
  if (capable->byte_order==LIVES_BIG_ENDIAN&&!prefs->bigendbug) {
    uint8_t xbuf[count];
    ssize_t retval=lives_read_buffered(fd,buf,count,allow_less);
    if (retval<count) return retval;
    reverse_bytes((uint8_t *)buf,(const uint8_t *)xbuf,count);
    return retval;
  }
  else {
    return lives_read_buffered(fd,buf,count,allow_less);
  }
}


ssize_t lives_write_buffered(int fd, const void *buf, size_t count, boolean allow_fail) {
  lives_file_buffer_t *fbuff;
  ssize_t retval=0,res;
  size_t space_left;

  if ((fbuff=find_in_file_buffers(fd))==NULL) {
    LIVES_DEBUG("lives_write_buffered: no file buffer found");
    return lives_write(fd,buf,count,allow_fail);
  }

  if (fbuff->read) {
    LIVES_ERROR("lives_write_buffered: wrong buffer type");
    return 0;
  }

  if (fbuff->buffer==NULL) {
    fbuff->buffer=(uint8_t *)g_malloc(BUFFER_FILL_BYTES);
    fbuff->ptr=fbuff->buffer;
    fbuff->bytes=0;
  }

  fbuff->allow_fail=allow_fail;

  // write bytes from fbuff 
  while (count) {
    space_left=BUFFER_FILL_BYTES-fbuff->bytes;

    if (space_left<count) {
      lives_memcpy(fbuff->ptr,buf,space_left);
      fbuff->bytes=BUFFER_FILL_BYTES;
      res=file_buffer_flush(fbuff);
      retval+=res;
      if (res<BUFFER_FILL_BYTES) return (res<0?res:retval);
      fbuff->bytes=0;
      fbuff->ptr=fbuff->buffer;
      count-=space_left;
      buf+=space_left;
    }
    else {
      lives_memcpy(fbuff->ptr,buf,count);
      retval+=count;
      fbuff->ptr+=count;
      fbuff->bytes+=count;
      count=0;
    }
  }
  return retval;
}


ssize_t lives_write_le_buffered(int fd, const void *buf, size_t count, boolean allow_fail) {
  if (capable->byte_order==LIVES_BIG_ENDIAN&&(prefs->bigendbug!=1)) {
    uint8_t xbuf[count];
    reverse_bytes(xbuf,(const uint8_t *)buf,count);
    return lives_write_buffered(fd,xbuf,count,allow_fail);
  }
  else {
    return lives_write_buffered(fd,buf,count,allow_fail);
  }

}

/////////////////////////////////////////////

char *lives_format_storage_space_string(uint64_t space) {
  char *fmt;

  if (space>lives_10pow(18)) {
    // TRANSLATORS: Exabytes
    fmt=g_strdup_printf(_("%.2f EB"),(double)space/(double)lives_10pow(18));
  }
  else if (space>lives_10pow(15)) {
    // TRANSLATORS: Petabytes
    fmt=g_strdup_printf(_("%.2f PB"),(double)space/(double)lives_10pow(15));
  }
  else if (space>lives_10pow(12)) {
    // TRANSLATORS: Terabytes
    fmt=g_strdup_printf(_("%.2f TB"),(double)space/(double)lives_10pow(12));
  }
  else if (space>lives_10pow(9)) {
    // TRANSLATORS: Gigabytes
    fmt=g_strdup_printf(_("%.2f GB"),(double)space/(double)lives_10pow(9));
  }
  else if (space>lives_10pow(6)) {
    // TRANSLATORS: Megabytes
    fmt=g_strdup_printf(_("%.2f MB"),(double)space/(double)lives_10pow(6));
  }
  else if (space>1024) {
    // TRANSLATORS: Kilobytes (1024 bytes)
    fmt=g_strdup_printf(_("%.2f KiB"),(double)space/1024.);
  }
  else {
    fmt=g_strdup_printf(_("%d bytes"),space);
  }

  return fmt;
}




lives_storage_status_t get_storage_status(const char *dir, uint64_t warn_level, uint64_t *dsval) {
  uint64_t ds;
  if (!is_writeable_dir(dir)) return LIVES_STORAGE_STATUS_UNKNOWN;
  ds=get_fs_free(dir);
  if (dsval!=NULL) *dsval=ds;
  if (ds<prefs->ds_crit_level) return LIVES_STORAGE_STATUS_CRITICAL;
  if (ds<warn_level) return LIVES_STORAGE_STATUS_WARNING;
  return LIVES_STORAGE_STATUS_NORMAL;
}




int lives_chdir(const char *path, boolean allow_fail) {
  int retval;

  retval=chdir(path);

  if (retval) {
    char *msg=g_strdup_printf("Chdir failed to: %s",path);
    mainw->chdir_failed=TRUE;
    if (!allow_fail) {
      LIVES_ERROR(msg);
      do_chdir_failed_error(path);
    }
    else LIVES_DEBUG(msg);
    g_free(msg);
  }
  return retval;
}



LIVES_INLINE void lives_freep(void **ptr) {
  // free a pointer and nullify it, only if it is non-null to start with
  // pass the address of the pointer in
  if (ptr!=NULL&&*ptr!=NULL) {
    g_free(*ptr);
    *ptr=NULL;
  }
}



#ifdef IS_MINGW



// should compile but it doesnt: http://msdn.microsoft.com/en-us/library/windows/desktop/ms683194%28v=vs.85%29.aspx
/*
typedef BOOL (WINAPI *LPFN_GLPI)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);


// Helper function to count set bits in the processor mask.
DWORD CountSetBits(ULONG_PTR bitMask) {
  DWORD LSHIFT = sizeof(ULONG_PTR)*8 - 1;
  DWORD bitSetCount = 0;
  ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT;    
  DWORD i;
    
  for (i = 0; i <= LSHIFT; ++i) {
    bitSetCount += ((bitMask & bitTest)?1:0);
    bitTest/=2;
  }
    
  return bitSetCount;
}


int lives_win32_get_num_logical_cpus(void) {
  LPFN_GLPI glpi;
  BOOL done = FALSE;
  PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
  PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = NULL;
  DWORD returnLength = 0;
  DWORD logicalProcessorCount = 0;
  DWORD numaNodeCount = 0;
  DWORD processorCoreCount = 0;
  DWORD processorL1CacheCount = 0;
  DWORD processorL2CacheCount = 0;
  DWORD processorL3CacheCount = 0;
  DWORD processorPackageCount = 0;
  DWORD byteOffset = 0;
  PCACHE_DESCRIPTOR Cache;

  glpi = (LPFN_GLPI) GetProcAddress(
				    GetModuleHandle(TEXT("kernel32")),
				    "GetLogicalProcessorInformation");
  if (NULL == glpi) {
    LIVES_WARN("GetLogicalProcessorInformation is not supported.");
    return 0;
  }

  while (!done) {
    DWORD rc = glpi(buffer, &returnLength);

    if (FALSE == rc) {
      if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
	if (buffer) g_free(buffer);
	  
	buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)g_malloc(returnLength);
	  
	if (NULL == buffer) {
	  LIVES_ERROR("Allocation failure");
	  return 0;
	}
      } 
      else {
	LIVES_ERROR("getting numprocs");
	return 0;
      }
    } 
    else {
      done = TRUE;
    }
  }

  ptr = buffer;

  while (byteOffset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= returnLength) {
    switch (ptr->Relationship) {
    case RelationNumaNode:
      // Non-NUMA systems report a single record of this type.
      numaNodeCount++;
      break;
	
    case RelationProcessorCore:
      processorCoreCount++;
	
      // A hyperthreaded core supplies more than one logical processor.
      logicalProcessorCount += CountSetBits(ptr->ProcessorMask);
      break;
	
    case RelationCache:
      // Cache data is in ptr->Cache, one CACHE_DESCRIPTOR structure for each cache. 
      Cache = &ptr->Cache;
      if (Cache->Level == 1)
	{
	  processorL1CacheCount++;
	}
      else if (Cache->Level == 2)
	{
	  processorL2CacheCount++;
	}
      else if (Cache->Level == 3)
	{
	  processorL3CacheCount++;
	}
      break;
	
    case RelationProcessorPackage:
      // Logical processors share a physical package.
      processorPackageCount++;
      break;
	
    default:
      _tprintf(TEXT("\nError: Unsupported LOGICAL_PROCESSOR_RELATIONSHIP value.\n"));
      break;
    }
    byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    ptr++;
  }
*/
  /*     _tprintf(TEXT("\nGetLogicalProcessorInformation results:\n"));
	 _tprintf(TEXT("Number of NUMA nodes: %d\n"), 
	 numaNodeCount);
	 _tprintf(TEXT("Number of physical processor packages: %d\n"), 
	 processorPackageCount);
	 _tprintf(TEXT("Number of processor cores: %d\n"), 
	 processorCoreCount);
	 _tprintf(TEXT("Number of logical processors: %d\n"), 
	 logicalProcessorCount);
	 _tprintf(TEXT("Number of processor L1/L2/L3 caches: %d/%d/%d\n"), 
	 processorL1CacheCount,
	 processorL2CacheCount,
	 processorL3CacheCount);*/
/*    
  g_free(buffer);

  return logicalProcessorCount;
}

*/








static boolean lives_win32_suspend_resume_threads(DWORD pid, boolean suspend) {
  HANDLE hThreadSnap;
  HANDLE hThread;
  THREADENTRY32 te32;

  // Take a snapshot of all threads in the system.
  hThreadSnap = CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, 0 );
  if( hThreadSnap == INVALID_HANDLE_VALUE ) {
    LIVES_ERROR("CreateToolhelp32Snapshot (of threades)");
    return FALSE;
  }

  // Set the size of the structure before using it.
  te32.dwSize = sizeof( THREADENTRY32 );

  // Retrieve information about the first thread,
  // and exit if unsuccessful
  if( !Thread32First( hThreadSnap, &te32 ) ) {
    LIVES_ERROR("Thread32First"); // show cause of failure
    CloseHandle( hThreadSnap );          // clean the snapshot object
    return( FALSE );
  }

  // Now walk the snapshot of threads, and suspend/resume any owned by process

  do {
    hThread = OpenThread( THREAD_SUSPEND_RESUME, FALSE, te32.th32ThreadID );
    if( hThread == NULL ) continue;

    if (te32.th32OwnerProcessID == pid) {
      if (suspend) {
	SuspendThread(hThread);
      }
      else {
	ResumeThread(hThread);
      }
    }

    CloseHandle( hThread );

  } while( Thread32Next( hThreadSnap, &te32 ) );

  CloseHandle( hThreadSnap );

  return TRUE;
 
}


boolean lives_win32_suspend_resume_process(DWORD pid, boolean suspend) {
  HANDLE hProcessSnap;
  HANDLE hProcess;
  PROCESSENTRY32 pe32;

  if (pid==0) return TRUE;

  // Take a snapshot of all processes in the system.
  hProcessSnap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
  if( hProcessSnap == INVALID_HANDLE_VALUE ) {
    LIVES_ERROR("CreateToolhelp32Snapshot (of processes)");
    return FALSE;
  }

  // Set the size of the structure before using it.
  pe32.dwSize = sizeof( PROCESSENTRY32 );

  // Retrieve information about the first process,
  // and exit if unsuccessful
  if( !Process32First( hProcessSnap, &pe32 ) ) {
    LIVES_ERROR("Process32First"); // show cause of failure
    CloseHandle( hProcessSnap );          // clean the snapshot object
    return( FALSE );
  }

  // resume this thread first
  if (!suspend) lives_win32_suspend_resume_threads(pid, FALSE);

  // Now walk the snapshot of processes, and
  // display information about each process in turn

  do {
    hProcess = OpenProcess( PROCESS_TERMINATE, FALSE, pe32.th32ProcessID );
    if( hProcess == NULL ) continue;

    // TODO - find equivalent on "real" windows
    if (!strcmp(pe32.szExeFile,"wineconsole.exe")) {
      CloseHandle( hProcess );
      continue;
    }

    if (pe32.th32ParentProcessID == pid && pe32.th32ProcessID != pid) {
      // suspend subprocess
      lives_win32_suspend_resume_process(pe32.th32ProcessID, suspend);
    }

    CloseHandle( hProcess );

  } while( Process32Next( hProcessSnap, &pe32 ) );
 
  CloseHandle( hProcessSnap );

  // suspend this thread last
  if (suspend) lives_win32_suspend_resume_threads(pid, TRUE);

  return TRUE;
}



boolean lives_win32_kill_subprocesses(DWORD pid, boolean kill_parent) {
  HANDLE hProcessSnap;
  HANDLE hProcess;
  PROCESSENTRY32 pe32;

  if (pid==0) return TRUE;

  // Take a snapshot of all processes in the system.
  hProcessSnap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
  if( hProcessSnap == INVALID_HANDLE_VALUE ) {
    LIVES_ERROR("CreateToolhelp32Snapshot (of processes)");
    return FALSE;
  }

  // Set the size of the structure before using it.
  pe32.dwSize = sizeof( PROCESSENTRY32 );

  // Retrieve information about the first process,
  // and exit if unsuccessful
  if( !Process32First( hProcessSnap, &pe32 ) ) {
    LIVES_ERROR("Process32First"); // show cause of failure
    CloseHandle( hProcessSnap );          // clean the snapshot object
    return( FALSE );
  }

  // Now walk the snapshot of processes, and
  // display information about each process in turn

  do {
    hProcess = OpenProcess( PROCESS_TERMINATE, FALSE, pe32.th32ProcessID );
    if( hProcess == NULL ) continue;

    //g_print("handling process %d %d : %s\n",pe32.th32ParentProcessID,pe32.th32ProcessID,pe32.szExeFile);

    // TODO - find equivalent on "real" windows
    if (!strcmp(pe32.szExeFile,"wineconsole.exe")) {
      CloseHandle( hProcess );
      continue;
    }

    if (pe32.th32ParentProcessID == pid && pe32.th32ProcessID != pid) {
      lives_win32_kill_subprocesses(pe32.th32ProcessID, TRUE);
    }

    CloseHandle( hProcess );

  } while( Process32Next( hProcessSnap, &pe32 ) );

  CloseHandle( hProcessSnap );

  if (kill_parent) {

    hProcessSnap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
    if( hProcessSnap == INVALID_HANDLE_VALUE ) {
      LIVES_ERROR("CreateToolhelp32Snapshot (of processes)");
      return FALSE;
    }
    
    // Set the size of the structure before using it.
    pe32.dwSize = sizeof( PROCESSENTRY32 );
    
    // Retrieve information about the first process,
    // and exit if unsuccessful
    if( !Process32First( hProcessSnap, &pe32 ) ) {
      LIVES_ERROR("Process32First"); // show cause of failure
      CloseHandle( hProcessSnap );          // clean the snapshot object
      return( FALSE );
    }
    
    // Now walk the snapshot of processes, and
    // display information about each process in turn
    
    do {
      hProcess = OpenProcess( PROCESS_TERMINATE, FALSE, pe32.th32ProcessID );
      if( hProcess == NULL ) continue;
      
      if (pe32.th32ProcessID == pid) {
	//g_print("killing %d\n", pe32.th32ProcessID);
	TerminateProcess(hProcess, 0);
      }

      CloseHandle( hProcess );
      
    } while( Process32Next( hProcessSnap, &pe32 ) );
    
    CloseHandle( hProcessSnap );

  }

  return TRUE;
}

#endif



LIVES_INLINE int lives_kill(lives_pid_t pid, int sig) {
#ifndef IS_MINGW
  if (pid==0) {
    LIVES_ERROR("Tried to kill pid 0");
    return -1;
  }
  return kill(pid,sig);
#else
  CloseHandle( pid->hProcess );
  CloseHandle( pid->hThread );
  GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid->dwProcessId);
#endif
  return 0;
};


LIVES_INLINE int lives_killpg(lives_pgid_t pgrp, int sig) {
#ifndef IS_MINGW
  return killpg(pgrp,sig);
#else
  CloseHandle( pgrp->hProcess );
  CloseHandle( pgrp->hThread );
  GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pgrp->dwProcessId);
#endif
  return 0;
};


LIVES_INLINE int myround(double n) {
  return (n>=0.)?(int)(n + 0.5):(int)(n - 0.5);
}

LIVES_INLINE void clear_mainw_msg (void) {
  memset (mainw->msg,0,512);
}


LIVES_INLINE uint64_t lives_10pow(int pow) {
  register int i;
  uint64_t res=1;
  for (i=0;i<pow;i++) res*=10;
  return res;
}

LIVES_INLINE int get_approx_ln(uint32_t x) {
  x |= (x >> 1);
  x |= (x >> 2);
  x |= (x >> 4);
  x |= (x >> 8);
  x |= (x >> 16);
  x++;
  return x>>1;
}


/**  return current (wallclock) time in ticks (units of 10 nanoseconds)
 */

int64_t lives_get_current_ticks(void) {
  gettimeofday(&tv, NULL);
  return U_SECL*tv.tv_sec+tv.tv_usec*U_SEC_RATIO;
}





/** set alarm for now + delta ticks (10 nanosec)
 * param ticks (10 nanoseconds) is the offset when we want our alarm to trigger 
 * returns int handle or -1
 * call lives_get_alarm(handle) to test if time arrived
 */

int lives_alarm_set(int64_t ticks) {
  int i;
  int64_t cticks;

  // we will assign [this] next
  int ret=mainw->next_free_alarm;

  // no alarm slots left
  if (mainw->next_free_alarm==-1) {
    LIVES_WARN("No alarms left");
    return -1;
  }

  // get current ticks
  cticks=lives_get_current_ticks();

  // set to now + offset
  mainw->alarms[mainw->next_free_alarm]=cticks+ticks;

  i=++mainw->next_free_alarm;

  // find free slot for next time
  while (mainw->alarms[i]!=LIVES_NO_ALARM_TICKS&&i<LIVES_MAX_ALARMS) {
    i++;
  }
  if (i==LIVES_MAX_ALARMS) {
    // no slots left
    mainw->next_free_alarm=-1;
  }
  // OK
  else mainw->next_free_alarm=i;

  return ret+1;
}


/*** check if alarm time passed yet, if so clear that alarm and return TRUE
 * else return FALSE
 */
boolean lives_alarm_get(int alarm_handle) {
  int64_t cticks;

  // invalid alarm number
  if (alarm_handle<=0 || alarm_handle > LIVES_MAX_ALARMS) {
    LIVES_WARN("Invalid get alarm handle");
    return FALSE;
  }

  // offset of 1 was added for caller
  alarm_handle--;

  // alarm time was never set !
  if (mainw->alarms[alarm_handle]==LIVES_NO_ALARM_TICKS) {
    LIVES_WARN("Alarm time not set");
    return TRUE;
  }

  // get current ticks
  cticks=lives_get_current_ticks();

  if (cticks>mainw->alarms[alarm_handle]) {
    // reached alarm time, free up this timer and return TRUE
    mainw->alarms[alarm_handle]=LIVES_NO_ALARM_TICKS;

    if (mainw->next_free_alarm==-1 || (alarm_handle<mainw->next_free_alarm)) {
      mainw->next_free_alarm=alarm_handle;
      mainw->alarms[alarm_handle]=LIVES_NO_ALARM_TICKS;
      LIVES_DEBUG("Alarm reached");
      return TRUE;
    }

  }

  // alarm time not reached yet
  return FALSE;
}



void lives_alarm_clear(int alarm_handle) {
  if (alarm_handle<=0 || alarm_handle > LIVES_MAX_ALARMS) {
    LIVES_WARN("Invalid clear alarm handle");
    return;
  }

  alarm_handle--;

  mainw->alarms[alarm_handle]=LIVES_NO_ALARM_TICKS;
  if (mainw->next_free_alarm==-1 || alarm_handle<mainw->next_free_alarm) 
    mainw->next_free_alarm=alarm_handle;
}



LIVES_INLINE char *g_strappend (char *string, int len, const char *xnew) {
  char *tmp=g_strconcat (string,xnew,NULL);
  g_snprintf(string,len,"%s",tmp);
  g_free(tmp);
  return string;
}


LIVES_INLINE GList *g_list_append_unique(GList *xlist, const char *add) {
  if (g_list_find_custom(xlist,add,(GCompareFunc)strcmp)==NULL) return g_list_append(xlist,g_strdup(add));
  return xlist;
}


LIVES_INLINE const char *get_image_ext_for_type(lives_image_type_t imgtype) {
  switch (imgtype) {
  case IMG_TYPE_JPEG: return "jpg";
  case IMG_TYPE_PNG: return "png";
  default: return "";
  }
}


/* convert to/from a big endian 32 bit float for internal use */
LIVES_INLINE float LEFloat_to_BEFloat(float f) {
  char *b=(char *)(&f);
  if (capable->byte_order==LIVES_LITTLE_ENDIAN) {
    float fl;
    guchar rev[4];
    rev[0]=b[3];
    rev[1]=b[2];
    rev[2]=b[1];
    rev[3]=b[0];
    fl=*(float *)rev;
    return fl;
  }
  return f;
}


LIVES_INLINE double calc_time_from_frame (int clip, int frame) {
  return (frame-1.)/mainw->files[clip]->fps;
}

LIVES_INLINE int calc_frame_from_time (int filenum, double time) {
  // return the nearest frame (rounded) for a given time, max is cfile->frames
  int frame=0;
  if (time<0.) return mainw->files[filenum]->frames?1:0;
  frame=(int)(time*mainw->files[filenum]->fps+1.49999);
  return (frame<mainw->files[filenum]->frames)?frame:mainw->files[filenum]->frames;
}

LIVES_INLINE int calc_frame_from_time2 (int filenum, double time) {
  // return the nearest frame (rounded) for a given time
  // allow max (frames+1)
  int frame=0;
  if (time<0.) return mainw->files[filenum]->frames?1:0;
  frame=(int)(time*mainw->files[filenum]->fps+1.49999);
  return (frame<mainw->files[filenum]->frames+1)?frame:mainw->files[filenum]->frames+1;
}

LIVES_INLINE int calc_frame_from_time3 (int filenum, double time) {
  // return the nearest frame (floor) for a given time
  // allow max (frames+1)
  int frame=0;
  if (time<0.) return mainw->files[filenum]->frames?1:0;
  frame=(int)(time*mainw->files[filenum]->fps+1.);
  return (frame<mainw->files[filenum]->frames+1)?frame:mainw->files[filenum]->frames+1;
}





static boolean check_for_audio_stop (int fileno, int first_frame, int last_frame) {
  // return FALSE if audio stops playback

#ifdef ENABLE_JACK
  if (prefs->audio_player==AUD_PLAYER_JACK&&mainw->jackd!=NULL&&mainw->jackd->playing_file==fileno) {
    if (!mainw->loop) {
      if (!mainw->loop_cont) {
	if (mainw->aframeno<first_frame||mainw->aframeno>last_frame) {
	  return FALSE;
	}
      }
    }
    else {
      if (!mainw->loop_cont) {
	if (mainw->aframeno<1||
	    calc_time_from_frame(mainw->current_file,mainw->aframeno)>cfile->laudio_time) { 
	  return FALSE;
	}
      }
    }
  }
#endif
#ifdef HAVE_PULSE_AUDIO
  if (prefs->audio_player==AUD_PLAYER_PULSE&&mainw->pulsed!=NULL&&mainw->pulsed->playing_file==fileno) {
    if (!mainw->loop) {
      if (!mainw->loop_cont) {
	if (mainw->aframeno<first_frame||mainw->aframeno>last_frame) {
	  return FALSE;
	}
      }
    }
    else {
      if (!mainw->loop_cont) {
	if (mainw->aframeno<1||
	    calc_time_from_frame(mainw->current_file,mainw->aframeno)>cfile->laudio_time) { 
	  return FALSE;
	}
      }
    }
  }
#endif
  return TRUE;
}


void calc_aframeno(int fileno) {
#ifdef ENABLE_JACK
  if (prefs->audio_player==AUD_PLAYER_JACK&&((mainw->jackd!=NULL&&mainw->jackd->playing_file==fileno)||
					     (mainw->jackd_read!=NULL&&mainw->jackd_read->playing_file==fileno))) {
    // get seek_pos from jack
    if (mainw->jackd_read!=NULL) mainw->aframeno=lives_jack_get_pos(mainw->jackd_read)/cfile->fps+1.;
    else mainw->aframeno=lives_jack_get_pos(mainw->jackd)/cfile->fps+1.;
  }
#endif
#ifdef HAVE_PULSE_AUDIO
  if (prefs->audio_player==AUD_PLAYER_PULSE&&((mainw->pulsed!=NULL&& mainw->pulsed->playing_file==fileno)||
					      (mainw->pulsed_read!=NULL&&mainw->pulsed_read->playing_file==fileno))) {
    // get seek_pos from pulse
    if (mainw->pulsed_read!=NULL) mainw->aframeno=lives_pulse_get_pos(mainw->pulsed_read)/cfile->fps+1.;
    else mainw->aframeno=lives_pulse_get_pos(mainw->pulsed)/cfile->fps+1.;
  }
#endif
}




int calc_new_playback_position(int fileno, uint64_t otc, uint64_t *ntc) {
  // returns a frame number (floor) using sfile->last_frameno and ntc-otc
  // takes into account looping modes

  // the range is first_frame -> last_frame

  // which is generally 1 -> sfile->frames, unless we are playing a selection

  // in case the frame is out of range and playing, returns 0 and sets mainw->cancelled 

  // ntc is adjusted backwards to timecode of the new frame


  // the basic operation is quite simple, given the time difference between the last frame and 
  // now, we calculate the new frame from the current fps and then ensure it is in the range
  // first_frame -> last_frame

  // Complications arise because we have ping-pong loop mode where the the play direction 
  // alternates - here we need to determine how many times we have reached the start or end 
  // play point. This is similar to the winding number in topological calculations.

  // caller should check return value of ntc, and if it differs from otc, show the frame


  // note we also calculate the audio "frame" and position for realtime audio players
  // this is done so we can check here if audio limits stopped playback


  int64_t dtc=*ntc-otc;
  file *sfile=mainw->files[fileno];

  int dir=0;
  int cframe,nframe;

  int first_frame,last_frame;

  boolean do_resync=FALSE;

  double fps;

  if (sfile==NULL) return 0;

  fps=sfile->pb_fps;

  if (mainw->playing_file==-1) fps=sfile->fps;

  cframe=sfile->last_frameno;

  if (fps==0.) {
    *ntc=otc;
    if (prefs->audio_src==AUDIO_SRC_INT) calc_aframeno(fileno);
    return cframe;
  }

  // dtc is delt ticks, quantise this to the frame rate and round down
  dtc=q_gint64_floor(dtc,fps);

  // ntc is the time when the frame should have been played
  *ntc=otc+dtc;

  // nframe is our new frame
  nframe=cframe+myround((double)dtc/U_SEC*fps);

  if (nframe==cframe||mainw->foreign) return nframe;

  // calculate audio "frame" from the number of samples played
  if (prefs->audio_src==AUDIO_SRC_INT&&mainw->playing_file==fileno) {
    calc_aframeno(fileno);
  }

  if (mainw->playing_file==fileno&&!mainw->clip_switched) {
    last_frame=(mainw->playing_sel&&!mainw->is_rendering)?sfile->end:mainw->play_end;
    if (last_frame>sfile->frames) last_frame=sfile->frames;
    first_frame=mainw->playing_sel?sfile->start:mainw->play_start;
    if (first_frame>sfile->frames) first_frame=sfile->frames;
  }
  else {
    last_frame=sfile->frames;
    first_frame=1;
  }


  if (mainw->playing_file==fileno) {

    if (mainw->noframedrop) {
      // if noframedrop is set, we may not skip any frames
      // - the usual situation is that we are allowed to skip frames
      if (nframe>cframe) nframe=cframe+1;
      else if (nframe<cframe) nframe=cframe-1;
    }

    // check if video stopped playback
    if (nframe<first_frame||nframe>last_frame) {
      if (mainw->whentostop==STOP_ON_VID_END) {
	mainw->cancelled=CANCEL_VID_END;
	return 0;
      }
    }


    // check if audio stopped playback
#ifdef RT_AUDIO
    if (mainw->whentostop==STOP_ON_AUD_END&&sfile->achans>0&&sfile->frames>0) {
      if (!check_for_audio_stop(fileno,first_frame,last_frame)) {
	mainw->cancelled=CANCEL_AUD_END;
	return 0;
      }
    }
#endif
  }
  
  if (sfile->frames==0) return 0;

  // get our frame back to within bounds
  
  nframe-=first_frame;

  if (fps>0) {
    dir=0;
    if (mainw->ping_pong) {
      dir=(int)((double)nframe/(double)(last_frame-first_frame+1));
      dir%=2;
    }
  }
  else {
    dir=1;
    if (mainw->ping_pong) {
      nframe-=(last_frame-first_frame);
      dir=(int)((double)nframe/(double)(last_frame-first_frame+1));
      dir%=2;
      dir++;
    }
  }

  nframe%=(last_frame-first_frame+1);

  if (fps<0) {
    // backwards
    if (dir==1) {
      // even winding
      if (!mainw->ping_pong) {
	// loop
	if (nframe<0) nframe+=last_frame+1;
	else nframe+=first_frame;
	if (nframe>cframe&&mainw->playing_file==fileno&&mainw->loop_cont&&!mainw->loop) {
	  // resync audio at end of loop section (playing backwards)
	  do_resync=TRUE;
	}
      }
      else {
	nframe+=last_frame; // normal
	if (nframe>last_frame) {
	  nframe=last_frame-(nframe-last_frame);
	  if (mainw->playing_file==fileno) dirchange_callback (NULL,NULL,0,(GdkModifierType)0,LIVES_INT_TO_POINTER(FALSE));
	  else sfile->pb_fps=-sfile->pb_fps;
	}
      }
    }
    else {
      // odd winding
      nframe=ABS(nframe)+first_frame;
      if (mainw->ping_pong) {
	// bounce
	if (mainw->playing_file==fileno) dirchange_callback (NULL,NULL,0,(GdkModifierType)0,LIVES_INT_TO_POINTER(FALSE));
	else sfile->pb_fps=-sfile->pb_fps;
      }
    }
  }
  else {
    // forwards
    nframe+=first_frame;
    if (dir==1) {
      // odd winding
      if (mainw->ping_pong) {
	// bounce
	nframe=last_frame-(nframe-(first_frame-1));
	if (mainw->playing_file==fileno) dirchange_callback (NULL,NULL,0,(GdkModifierType)0,LIVES_INT_TO_POINTER(FALSE));
	else sfile->pb_fps=-sfile->pb_fps;
      }
    }
    else if (mainw->playing_sel&&!mainw->ping_pong&&mainw->playing_file==fileno&&nframe<cframe&&mainw->loop_cont&&!mainw->loop) {
      // resync audio at start of loop selection
      if (nframe<first_frame) {
	nframe=last_frame-(first_frame-nframe)+1;
      }
      do_resync=TRUE;
    }
    if (nframe<first_frame) {
      // scratch or transport backwards
      if (mainw->ping_pong) {
	nframe=first_frame;
	if (mainw->playing_file==fileno) dirchange_callback (NULL,NULL,0,(GdkModifierType)0,LIVES_INT_TO_POINTER(FALSE));
	else sfile->pb_fps=-sfile->pb_fps;

      }
      else nframe=last_frame-nframe;
    }
  }

  if (nframe<first_frame) nframe=first_frame;
  if (nframe>last_frame) nframe=last_frame;

  if (prefs->audio_opts&AUDIO_OPTS_FOLLOW_FPS) {
    if (do_resync||(mainw->scratch!=SCRATCH_NONE&&mainw->playing_file==fileno)) {
      boolean is_jump=FALSE;
      if (mainw->scratch==SCRATCH_JUMP) is_jump=TRUE;
      mainw->scratch=SCRATCH_NONE;
      if (sfile->achans>0) {
	resync_audio(nframe);
      }
      if (is_jump) mainw->video_seek_ready=TRUE;
      if (mainw->whentostop==STOP_ON_AUD_END&&sfile->achans>0) {
	// we check for audio stop here, but the seek may not have happened yet
	if (!check_for_audio_stop(fileno,first_frame,last_frame)) {
	  mainw->cancelled=CANCEL_AUD_END;
	  return 0;
	}
      }
    }
  }

  return nframe;
}





void calc_maxspect(int rwidth, int rheight, int *cwidth, int *cheight) {
  // calculate maxspect (maximum size which maintains aspect ratio)
  // of cwidth, cheight - given restrictions rwidth * rheight

  double aspect;

  if (*cwidth<=0||*cheight<=0||rwidth<=0||rheight<=0) return;

  if (*cwidth>rwidth) {
    // image too wide shrink it
    aspect=(double)rwidth/(double)(*cwidth);
    *cwidth=rwidth;
    *cheight=(double)(*cheight)*aspect;
  }
  if (*cheight>rheight) {
    // image too tall shrink it
    aspect=(double)rheight/(double)(*cheight);
    *cheight=rheight;
    *cwidth=(double)(*cwidth)*aspect;
  }

  aspect=(double)*cwidth/(double)*cheight;

  if ((double)rheight*aspect<=rwidth) {
    // bound by rheight
    *cheight=rheight;
    *cwidth=((double)rheight*aspect+.5);
    if (*cwidth>rwidth) *cwidth=rwidth;
  }
  else {
    // bound by rwidth
    *cwidth=rwidth;
    *cheight=((double)rwidth/aspect+.5);
    if (*cheight>rheight) *cheight=rheight;
  }
}




/////////////////////////////////////////////////////////////////////////////


void init_clipboard(void) {
  int current_file=mainw->current_file;
  char *com;

  if (clipboard==NULL) {
    // here is where we create the clipboard
    // use get_new_handle(clipnumber,name);
    if (!get_new_handle(0,"clipboard")) {
      mainw->error=TRUE;
      return;
    }
  }
  else if (clipboard->frames>0) { 
    // clear old clipboard
    // need to set current file to 0 before monitoring progress
    mainw->current_file=0;
    mainw->com_failed=FALSE;
    com=g_strdup_printf("%s delete_all \"%s\"",prefs->backend,clipboard->handle);
    unlink(clipboard->info_file);
    lives_system(com,FALSE);
    g_free(com);

    if (mainw->com_failed) {
      mainw->current_file=current_file;
      return;
    }

    cfile->progress_start=cfile->start;
    cfile->progress_end=cfile->end;
    // show a progress dialog, not cancellable
    do_progress_dialog(TRUE,FALSE,_ ("Clearing the clipboard"));
  }

  mainw->current_file=current_file;
}



void d_print(const char *text) {
  // print out output in the main message area (and info log)

  

  // there are several small tweaks for this:

  // mainw->suppress_dprint :: TRUE - dont print anything, return (for silencing noisy message blocks)
  // mainw->no_switch_dprint :: TRUE - disable printing of switch message when maine->current_file changes

  // mainw->last_dprint_file :: clip number of last mainw->current_file;
  char *switchtext,*tmp;

  GtkTextIter end_iter;
  GtkTextMark *mark;

  GtkTextBuffer *tbuf=gtk_text_view_get_buffer(GTK_TEXT_VIEW(mainw->textview1));

  if (!capable->smog_version_correct) return;

  if (mainw->suppress_dprint) return;

  if (LIVES_IS_TEXT_VIEW (mainw->textview1)) {
    gtk_text_buffer_get_end_iter(tbuf,&end_iter);
    gtk_text_buffer_insert(tbuf,&end_iter,text,-1);
    if (mainw->current_file!=mainw->last_dprint_file&&mainw->current_file!=0&&mainw->multitrack==NULL&&
	(mainw->current_file==-1||cfile->clip_type!=CLIP_TYPE_GENERATOR)&&!mainw->no_switch_dprint) {
      if (mainw->current_file>0) {
	switchtext=g_strdup_printf (_ ("\n==============================\nSwitched to clip %s\n"),
				    cfile->clip_type!=CLIP_TYPE_VIDEODEV?(tmp=g_path_get_basename(cfile->name)):
				    (tmp=g_strdup(cfile->name)));
	g_free(tmp);
      }
      else {
	switchtext=g_strdup (_ ("\n==============================\nSwitched to empty clip\n"));
      }
      gtk_text_buffer_get_end_iter(tbuf,&end_iter);
      gtk_text_buffer_insert(tbuf,&end_iter,switchtext,-1);
      g_free (switchtext);
    }
    if ((mainw->current_file==-1||cfile->clip_type!=CLIP_TYPE_GENERATOR)&&
	(!mainw->no_switch_dprint||mainw->current_file!=0)) mainw->last_dprint_file=mainw->current_file;
    gtk_text_buffer_get_end_iter(tbuf,&end_iter);
    mark=gtk_text_buffer_create_mark(tbuf,NULL,&end_iter,FALSE);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW (mainw->textview1),mark);
    gtk_text_buffer_delete_mark (tbuf,mark);
  }
}



boolean add_lmap_error(lives_lmap_error_t lerror, const char *name, livespointer user_data, int clipno, 
			int frameno, double atime, boolean affects_current) {
  // potentially add a layout map error to the layout textbuffer
  GtkTextIter end_iter;
  char *text,*name2;
  char **array;
  GList *lmap;
  double orig_fps;
  int resampled_frame;
  double max_time;

  gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter);

  if (affects_current&&user_data==NULL) {
    mainw->affected_layout_marks=g_list_append(mainw->affected_layout_marks,
					       (gpointer)gtk_text_buffer_create_mark
					       (GTK_TEXT_BUFFER(mainw->layout_textbuffer),NULL,&end_iter,TRUE));
  }

  switch (lerror) {
  case LMAP_INFO_SETNAME_CHANGED:
    if (strlen(name)==0) name2=g_strdup(_("(blank)"));
    else name2=g_strdup(name);
    text=g_strdup_printf
      (_("The set name has been changed from %s to %s. Affected layouts have been updated accordingly\n"),
       name2,(char *)user_data);
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter,text,-1);
    g_free(name2);
    g_free(text);
    break;
  case LMAP_ERROR_MISSING_CLIP:
    if (prefs->warning_mask&WARN_MASK_LAYOUT_MISSING_CLIPS) return FALSE;
    text=g_strdup_printf(_("The clip %s is missing from this set.\nIt is required by the following layouts:\n"),name);
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter,text,-1);
    g_free(text);
  case LMAP_ERROR_CLOSE_FILE:
    text=g_strdup_printf(_("The clip %s has been closed.\nIt is required by the following layouts:\n"),name);
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter,text,-1);
    g_free(text);
    break;
  case LMAP_ERROR_SHIFT_FRAMES:
    text=g_strdup_printf(_("Frames have been shifted in the clip %s.\nThe following layouts are affected:\n"),name);
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter,text,-1);
    g_free(text);
    break;
  case LMAP_ERROR_DELETE_FRAMES:
    text=g_strdup_printf(_("Frames have been deleted from the clip %s.\nThe following layouts are affected:\n"),name);
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter,text,-1);
    g_free(text);
    break;
  case LMAP_ERROR_DELETE_AUDIO:
    text=g_strdup_printf(_("Audio has been deleted from the clip %s.\nThe following layouts are affected:\n"),name);
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter,text,-1);
    g_free(text);
    break;
  case LMAP_ERROR_SHIFT_AUDIO:
    text=g_strdup_printf(_("Audio has been shifted in clip %s.\nThe following layouts are affected:\n"),name);
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter,text,-1);
    g_free(text);
    break;
  case LMAP_ERROR_ALTER_AUDIO:
    text=g_strdup_printf(_("Audio has been altered in the clip %s.\nThe following layouts are affected:\n"),name);
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter,text,-1);
    g_free(text);
    break;
  case LMAP_ERROR_ALTER_FRAMES:
    text=g_strdup_printf(_("Frames have been altered in the clip %s.\nThe following layouts are affected:\n"),name);
    gtk_text_buffer_insert(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter,text,-1);
    g_free(text);
    break;
  }

  if (affects_current&&user_data!=NULL) {
    mainw->affected_layout_marks=g_list_append(mainw->affected_layout_marks,
					       (gpointer)gtk_text_buffer_create_mark
					       (GTK_TEXT_BUFFER(mainw->layout_textbuffer),NULL,&end_iter,TRUE));
  }

  switch (lerror) {
  case LMAP_INFO_SETNAME_CHANGED:
    lmap=mainw->current_layouts_map;
    while (lmap!=NULL) {
      array=g_strsplit((char *)lmap->data,"|",-1);
      text=g_strdup_printf("%s\n",array[0]);
      gtk_text_buffer_insert(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter,text,-1);
      g_free(text);
      //mainw->affected_layouts_map=g_list_append_unique(mainw->affected_layouts_map,array[0]);
      g_strfreev(array);
      lmap=lmap->next;
    }
    break;
  case LMAP_ERROR_MISSING_CLIP:
  case LMAP_ERROR_CLOSE_FILE:
    if (affects_current) {
      text=g_strdup_printf("%s\n",mainw->string_constants[LIVES_STRING_CONSTANT_CL]);
      gtk_text_buffer_insert(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter,text,-1);
      g_free(text);
      mainw->affected_layouts_map=g_list_append_unique(mainw->affected_layouts_map,mainw->string_constants[LIVES_STRING_CONSTANT_CL]);

      mainw->affected_layout_marks=g_list_append(mainw->affected_layout_marks,(gpointer)gtk_text_buffer_create_mark(GTK_TEXT_BUFFER(mainw->layout_textbuffer),NULL,&end_iter,TRUE));

    }
    lmap=(GList *)user_data;
    while (lmap!=NULL) {
      array=g_strsplit((char *)lmap->data,"|",-1);
      text=g_strdup_printf("%s\n",array[0]);
      gtk_text_buffer_insert(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter,text,-1);
      g_free(text);
      mainw->affected_layouts_map=g_list_append_unique(mainw->affected_layouts_map,array[0]);
      g_strfreev(array);
      lmap=lmap->next;
    }
    break;
  case LMAP_ERROR_SHIFT_FRAMES:
  case LMAP_ERROR_DELETE_FRAMES:
  case LMAP_ERROR_ALTER_FRAMES:
    if (affects_current) {
      text=g_strdup_printf("%s\n",mainw->string_constants[LIVES_STRING_CONSTANT_CL]);
      gtk_text_buffer_insert(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter,text,-1);
      g_free(text);
      mainw->affected_layouts_map=g_list_append_unique(mainw->affected_layouts_map,mainw->string_constants[LIVES_STRING_CONSTANT_CL]);

      mainw->affected_layout_marks=g_list_append(mainw->affected_layout_marks,(gpointer)gtk_text_buffer_create_mark(GTK_TEXT_BUFFER(mainw->layout_textbuffer),NULL,&end_iter,TRUE));
    }
    lmap=(GList *)user_data;
    while (lmap!=NULL) {
      array=g_strsplit((char *)lmap->data,"|",-1);
      orig_fps=strtod(array[3],NULL);
      resampled_frame=count_resampled_frames(frameno,orig_fps,mainw->files[clipno]->fps);
      if (resampled_frame<=atoi(array[2])) {
	text=g_strdup_printf("%s\n",array[0]);
	gtk_text_buffer_insert(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter,text,-1);
	g_free(text);
	mainw->affected_layouts_map=g_list_append_unique(mainw->affected_layouts_map,array[0]);
      }
      g_strfreev(array);
      lmap=lmap->next;
    }
    break;
  case LMAP_ERROR_SHIFT_AUDIO:
  case LMAP_ERROR_DELETE_AUDIO:
  case LMAP_ERROR_ALTER_AUDIO:
    if (affects_current) {
      text=g_strdup_printf("%s\n",mainw->string_constants[LIVES_STRING_CONSTANT_CL]);
      gtk_text_buffer_insert(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter,text,-1);
      g_free(text);
      mainw->affected_layouts_map=g_list_append_unique(mainw->affected_layouts_map,mainw->string_constants[LIVES_STRING_CONSTANT_CL]);

      mainw->affected_layout_marks=g_list_append(mainw->affected_layout_marks,(gpointer)gtk_text_buffer_create_mark(GTK_TEXT_BUFFER(mainw->layout_textbuffer),NULL,&end_iter,TRUE));
    }
    lmap=(GList *)user_data;
    while (lmap!=NULL) {
      array=g_strsplit((char *)lmap->data,"|",-1);
      max_time=strtod(array[4],NULL);
      if (max_time>0.&&atime<=max_time) {
	text=g_strdup_printf("%s\n",array[0]);
	gtk_text_buffer_insert(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter,text,-1);
	g_free(text);
	mainw->affected_layouts_map=g_list_append_unique(mainw->affected_layouts_map,array[0]);
      }
      g_strfreev(array);
      lmap=lmap->next;
    }
    break;
  }

  lives_widget_set_sensitive (mainw->show_layout_errors, TRUE);
  if (mainw->multitrack!=NULL) lives_widget_set_sensitive (mainw->multitrack->show_layout_errors, TRUE);
  return TRUE;
}


void clear_lmap_errors(void) {
  GtkTextIter start_iter,end_iter;
  GList *lmap;

  gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&start_iter);
  gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&end_iter);
  gtk_text_buffer_delete(GTK_TEXT_BUFFER(mainw->layout_textbuffer),&start_iter,&end_iter);

  lmap=mainw->affected_layouts_map;

  while (lmap!=NULL) {
    g_free(lmap->data);
    lmap=lmap->next;
  }
  g_list_free(lmap);

  mainw->affected_layouts_map=NULL;
  lives_widget_set_sensitive (mainw->show_layout_errors, FALSE);
  if (mainw->multitrack!=NULL) lives_widget_set_sensitive (mainw->multitrack->show_layout_errors, FALSE);

  if (mainw->affected_layout_marks!=NULL) {
    remove_current_from_affected_layouts(mainw->multitrack);
  }

}


boolean check_for_lock_file(const char *set_name, int type) {
  // check for lock file
  // do this via the back-end (smogrify)
  // this allows for the locking scheme to be more flexible

  // smogrify indicates a lock very simply by by writing >0 bytes to stdout
  // we redirect the output to info_file and read it

  int info_fd;
  char *msg=NULL;
  ssize_t bytes;

  char *info_file=g_strdup_printf("%s/.locks.%d",prefs->tmpdir,getpid());
  char *com=g_strdup_printf("%s check_for_lock \"%s\" \"%s\" %d >\"%s\"",prefs->backend_sync,set_name,capable->myname,
			     getpid(),info_file);

  unlink(info_file);
  threaded_dialog_spin();
  mainw->com_failed=FALSE;
  lives_system(com,FALSE);
  threaded_dialog_spin();
  g_free(com);

  clear_mainw_msg();

  if (mainw->com_failed) return FALSE;

  info_fd=open(info_file,O_RDONLY);
  if (info_fd>-1) {
    if ((bytes=read(info_fd,mainw->msg,256))>0) {
      close(info_fd);
      memset(mainw->msg+bytes,0,1);

      if (type==0) {
	msg=g_strdup_printf(_("Set %s\ncannot be opened, as it is in use\nby another copy of LiVES.\n"),set_name);
	threaded_dialog_spin();
	do_error_dialog(msg);
	threaded_dialog_spin();
      }
      else if (type==1) {
	msg=g_strdup_printf
	  (_("\nThe set %s is currently in use by another copy of LiVES.\nPlease choose another set name.\n"),set_name);
	if (!mainw->osc_auto) do_blocking_error_dialog(msg);
      }
      if (msg!=NULL) {
	g_free(msg);
      }
      unlink(info_file);
      g_free(info_file);
      return FALSE;
    }
  }
  close (info_fd);
  unlink(info_file);
  g_free(info_file);

  return TRUE;
}


boolean is_legal_set_name(const char *set_name, boolean allow_dupes) {
  // check (clip) set names for validity
  // - may not be of zero length
  // - may not contain spaces or characters / \ * "
  // - must NEVER be name of a set in use by another copy of LiVES (i.e. with a lock file)

  // - as of 1.6.0:
  // -  may not start with a .
  // -  may not contain ..

  // may not be longer than 128 chars

  // iff allow_dupes is FALSE then we disallow the name of any existing set (has a subdirectory in the working directory)

  int i;

  char *msg;
  char *reject=" /\\*\"";
  size_t slen=strlen(set_name);

  if (slen==0) {
    if (!mainw->osc_auto) do_blocking_error_dialog(_("\nSet names may not be blank.\n"));
    return FALSE;
  }

  if (slen>128) {
    if (!mainw->osc_auto) do_blocking_error_dialog(_("\nSet names may not be longer than 128 characters.\n"));
    return FALSE;
  }

  if (strcspn(set_name,reject)!=slen) {
    msg=g_strdup_printf(_("\nSet names may not contain spaces or the characters%s.\n"),reject);
    if (!mainw->osc_auto) do_blocking_error_dialog(msg);
    g_free(msg);
    return FALSE;
  }

  for (i=0;i<slen;i+=2) {
    if (set_name[i]=='.'&&((i==0||set_name[i-1]=='.')||(i<slen-1&&set_name[i+1]=='.'))) {
      msg=g_strdup(_("\nSet names may not start with a '.' or contain '..'\n"));
      if (!mainw->osc_auto) do_blocking_error_dialog(msg);
      g_free(msg);
      return FALSE;
    }
  }

  // check if this is a set in use by another copy of LiVES
  if (!check_for_lock_file(set_name,1)) return FALSE;
  
  if (!allow_dupes) {
    // check for duplicate set names
    char *set_dir=g_build_filename(prefs->tmpdir,set_name,NULL);
    if (g_file_test(set_dir,G_FILE_TEST_IS_DIR)) {
      g_free(set_dir);
      msg=g_strdup_printf(_("\nThe set %s already exists.\nPlease choose another set name.\n"),set_name);
      do_blocking_error_dialog(msg);
      g_free(msg);
      return FALSE;
    }
    g_free(set_dir);
  }

  return TRUE;
}




boolean check_frame_count(int idx) {
  // check number of frames is correct
  // for files of type CLIP_TYPE_DISK
  // - check the image files (e.g. jpeg or png)

  // use a "goldilocks" algorithm (just the right frames, not too few and not too many)

  // ingores gaps

  // make sure nth frame is there...
  char *frame=g_strdup_printf("%s/%s/%08d.%s",prefs->tmpdir,mainw->files[idx]->handle,mainw->files[idx]->frames,
			       get_image_ext_for_type(mainw->files[idx]->img_type));

  if (!g_file_test(frame,G_FILE_TEST_EXISTS)) {
    // not enough frames
    g_free(frame);
    return FALSE;
  }
  g_free(frame);

  // ...make sure n + 1 th frame is not
  frame=g_strdup_printf("%s/%s/%08d.%s",prefs->tmpdir,mainw->files[idx]->handle,mainw->files[idx]->frames+1,
			get_image_ext_for_type(mainw->files[idx]->img_type));
  if (g_file_test(frame,G_FILE_TEST_EXISTS)) {
    // too many frames
    g_free(frame);
    return FALSE;
  }
  g_free(frame);

  // just right
  return TRUE;
}



void get_frame_count(int idx) {
  // sets mainw->files[idx]->frames with current framecount

  // calls smogrify which physically finds the last frame using a (fast) O(log n) binary search method

  // for CLIP_TYPE_DISK only

  // (CLIP_TYPE_FILE should use the decoder plugin frame count)

  int info_fd;
  int retval;
  ssize_t bytes;
  char *info_file=g_strdup_printf("%s/.check.%d",prefs->tmpdir,getpid());
  char *com=g_strdup_printf("%s count_frames \"%s\" \"%s\" > \"%s\"",prefs->backend_sync,mainw->files[idx]->handle,
			     get_image_ext_for_type(mainw->files[idx]->img_type),info_file);

  mainw->com_failed=FALSE;
  lives_system(com,FALSE);
  g_free(com);
  
  if (mainw->com_failed) {
    g_free(info_file);
    return;
  }

  do {
    retval=0;
    info_fd=open(info_file,O_RDONLY);
    if (info_fd<0) {
      retval=do_read_failed_error_s_with_retry(info_file,g_strerror(errno),NULL);
    }
    else {
      if ((bytes=lives_read(info_fd,mainw->msg,256,TRUE))>0) {
	if (bytes==0) {
	  retval=do_read_failed_error_s_with_retry(info_file,NULL,NULL);
	}
	else {
	  memset(mainw->msg+bytes,0,1);
	  mainw->files[idx]->frames=atoi(mainw->msg);
	}
      }
      close(info_fd);
    }
  } while (retval==LIVES_RETRY);

  unlink(info_file);
  g_free(info_file);
}


void get_frames_sizes(int fileno, int frame) {
  file *sfile=mainw->files[fileno];
  LiVESPixbuf *pixbuf;
  
  if ((pixbuf=pull_lives_pixbuf(fileno,frame,get_image_ext_for_type(mainw->files[fileno]->img_type),0))) {
    sfile->hsize=lives_pixbuf_get_width(pixbuf);
    sfile->vsize=lives_pixbuf_get_height(pixbuf);
    g_object_unref(pixbuf);
  }

}



void get_next_free_file(void) {
  // get next free file slot, or -1 if we are full
  // can support MAX_FILES files (default 65536)
  while ((mainw->first_free_file!=-1)&&mainw->files[mainw->first_free_file]!=NULL) {
    mainw->first_free_file++;
    if (mainw->first_free_file>=MAX_FILES) mainw->first_free_file=-1;
  }
}


void get_dirname(char *filename) {
  char *tmp;
  // get directory name from a file
  //filename should point to char[PATH_MAX]

  g_snprintf (filename,PATH_MAX,"%s%s",(tmp=g_path_get_dirname (filename)),G_DIR_SEPARATOR_S);
  g_free(tmp);

  if (!strcmp(filename,"//")) {
    memset(filename+1,0,1);
    return;
  }

  if (!strncmp(filename,"./",2)) {
    char *tmp1=g_get_current_dir(),*tmp=g_build_filename(tmp1,filename+2,NULL);
    g_free(tmp1);
    g_snprintf(filename,PATH_MAX,"%s",tmp);
    g_free(tmp);
  }
}


char *get_dir(const char *filename) {
  char tmp[PATH_MAX];
  g_snprintf(tmp,PATH_MAX,"%s",filename);
  get_dirname(tmp);
  return g_strdup(tmp);
}


void get_basename(char *filename) {
  // get basename from a file
  // (filename without directory)
  //filename should point to char[PATH_MAX]
  char *tmp=g_path_get_basename(filename);
  g_snprintf (filename,PATH_MAX,"%s",tmp);
  g_free(tmp);
}

void get_filename(char *filename, boolean strip_dir) {
  // get filename (part without extension) of a file
  //filename should point to char[PATH_MAX]
  char **array;
  if (strip_dir) get_basename(filename);
  array=g_strsplit(filename,".",-1);
  g_snprintf(filename,PATH_MAX,"%s",array[0]);
  g_strfreev(array);
}


char *get_extension(const char *filename) {
  char *tmp=g_path_get_basename(filename);
  int ntok=get_token_count((char *)filename,'.');
  char **array=g_strsplit(tmp,".",-1);
  char *ret=g_strdup(array[ntok-1]);
  g_strfreev(array);
  g_free(tmp);
  return ret;
}


char *ensure_extension(const char *fname, const char *ext) {
  if (!strcmp(fname+strlen(fname)-strlen(ext),ext)) return g_strdup(fname);
  return g_strconcat(fname,ext,NULL);
}


boolean ensure_isdir(char *fname) {
  // ensure dirname ends in a single dir separator
  // fname should be char[PATH_MAX]

  // returns TRUE if fname was altered

  size_t slen=strlen(fname);
  size_t offs=slen-1;
  char *tmp;

  while (offs>=0&&!strcmp(fname+offs,G_DIR_SEPARATOR_S)) offs--;
  if (offs==slen-2) return FALSE;
  memset(fname+offs+1,0,1);
  tmp=g_strdup_printf("%s%s",fname,G_DIR_SEPARATOR_S);
  g_snprintf(fname,PATH_MAX,"%s",tmp);
  g_free(tmp);
  return TRUE;
}


void get_location(const char *exe, char *val, int maxlen) {
  // find location of "exe" in path
  // sets it in val which is a char array of maxlen bytes

  char *loc;
  if ((loc=g_find_program_in_path (exe))!=NULL) {
    g_snprintf (val,maxlen,"%s",loc);
    g_free (loc);
  }
  else {
    memset (val,0,1);
  }
}


uint64_t get_version_hash(const char *exe, const char *sep, int piece) {
  /// get version hash output for an executable from the backend
  FILE *rfile;
  ssize_t rlen;
  char val[16];
  char *com=g_strdup_printf("%s get_version_hash \"%s\" \"%s\" %d",prefs->backend_sync,exe,sep,piece);
  rfile=popen(com,"r");
  rlen=fread(val,1,16,rfile);
  pclose(rfile);
  memset(val+rlen,0,1);
  g_free(com);
  return strtol(val,NULL,10);
}

#define VER_MAJOR_MULT 1000000
#define VER_MINOR_MULT 1000
#define VER_MICRO_MULT 1

uint64_t make_version_hash(const char *ver) {
  /// convert a version to uint64_t hash, for comparing

  uint64_t hash;
  int ntok;
  char **array;

  if (ver==NULL) return 0;

  ntok=get_token_count((char *)ver,'.');
  array=g_strsplit(ver,".",-1);

  hash=atoi(array[0])*VER_MAJOR_MULT;

  if (ntok>1) {
    hash+=atoi(array[1])*VER_MINOR_MULT;
  }

  if (ntok>2) {
    hash+=atoi(array[2])*VER_MICRO_MULT;
  }

  g_strfreev(array);

  return hash;
}



char *repl_tmpdir(const char *entry, boolean fwd) {
  // replace prefs->tmpdir with string tmpdir or vice-versa. This allows us to relocate tmpdir if necessary.
  // used for layout.map file
  // return value should be g_free()'d

  // fwd TRUE replaces "/tmp/foo" with "tmpdir"
  // fwd FALSE replaces "tmpdir" with "/tmp/foo"


  char *string=g_strdup(entry);;

  if (fwd) {
    if (!strncmp(entry,prefs->tmpdir,strlen(prefs->tmpdir))) {
      g_free(string);
      string=g_strdup_printf("tmpdir%s",entry+strlen(prefs->tmpdir));
    }
  }
  else {
    if (!strncmp(entry,"tmpdir",6)) {
      g_free(string);
      string=g_build_filename(prefs->tmpdir,entry+6,NULL);
    }
  }
  return string;
}


void remove_layout_files(GList *map) {
  // removes a GList of layouts from the set layout map

  // removes from: - global layouts map
  //               - disk
  //               - clip layout maps

  // called after, for example: a clip is removed or altered and the user opts to remove all associated layouts

  char *com,*msg;
  char *fname,*fdir;
  char **array;
  GList *lmap,*lmap_next,*cmap,*cmap_next,*map_next;
  size_t maplen;
  int i;
  boolean is_current;

  while (map!=NULL) {
    map_next=map->next;
    if (map->data!=NULL) {
      if (!strcmp((char *)map->data,mainw->string_constants[LIVES_STRING_CONSTANT_CL])) {
	is_current=TRUE;
	fname=g_strdup(mainw->string_constants[LIVES_STRING_CONSTANT_CL]);
      }
      else {
	is_current=FALSE;
	maplen=strlen((char *)map->data);
	
	// remove from mainw->current_layouts_map
	cmap=mainw->current_layouts_map;
	while (cmap!=NULL) {
	  cmap_next=cmap->next;
	  if (!strcmp((char *)cmap->data,(char *)map->data)) {
	    mainw->current_layouts_map=g_list_remove_link(mainw->current_layouts_map,cmap);
	    break;
	  }
	  cmap=cmap_next;
	}

	array=g_strsplit((char *)map->data,"|",-1);
	fname=repl_tmpdir(array[0],FALSE);
	g_strfreev(array);
      }

      // fname should now hold the layout name on disk

      msg=g_strdup_printf(_("Removing layout %s\n"),fname);
      d_print(msg);
      g_free(msg);

      if (!is_current) {
#ifndef IS_MINGW
	com=g_strdup_printf("/bin/rm \"%s\" 2>/dev/null",fname);
#else
	com=g_strdup_printf("rm.exe \"%s\" 2>NUL",fname);
#endif
	lives_system(com,TRUE);
	g_free(com);
	

	// if no more layouts in parent dir, we can delete dir

	// ensure that parent dir is below our own working dir
	if (!strncmp(fname,prefs->tmpdir,strlen(prefs->tmpdir))) {
	  // is in tmpdir, safe to remove parents

	  char *protect_file=g_build_filename(prefs->tmpdir,"noremove",NULL);

	  mainw->com_failed=FALSE;
	  // touch a file in tpmdir, so we cannot remove tmpdir itself
#ifndef IS_MINGW
	  com=g_strdup_printf("/bin/touch \"%s\" >/dev/null 2>&1",protect_file);
#else
	  com=g_strdup_printf("touch.exe \"%s\" >NUL 2>&1",protect_file);
#endif
	  lives_system(com,FALSE);
	  g_free(com);
	  
	  if (!mainw->com_failed) {
	    // ok, the "touch" worked
	    // now we call rmdir -p : remove directory + any empty parents
	    fdir=g_path_get_dirname (fname);
#ifndef IS_MINGW
	    com=g_strdup_printf("/bin/rmdir -p \"%s\" 2>/dev/null",fdir);
#else
	    com=g_strdup_printf("rmdir.exe /p \"%s\" 2>NUL",fdir);
#endif
	    lives_system(com,TRUE);
	    g_free(com);
	    g_free(fdir);
	  }

	  // remove the file we touched to clean up
	  unlink(protect_file);
	  g_free(protect_file);
	}
	

	// remove from mainw->files[]->layout_map
	for (i=1;i<=MAX_FILES;i++) {
	  if (mainw->files[i]!=NULL) {
	    if (mainw->files[i]->layout_map!=NULL) {
	      lmap=mainw->files[i]->layout_map;
	      while (lmap!=NULL) {
		lmap_next=lmap->next;
		if (!strncmp((char *)lmap->data,(char *)map->data,maplen)) {
		  // remove matching entry
		  if (lmap->prev!=NULL) lmap->prev->next=lmap_next;
		  else mainw->files[i]->layout_map=lmap_next;
		  if (lmap->next!=NULL) lmap_next->prev=lmap->prev;
		  lmap->next=lmap->prev=NULL;
		  g_free(lmap->data);
		  g_list_free(lmap);
		}
		lmap=lmap_next;
	      }
	    }
	  }
	}
      }
      else {
	// asked to remove the currently loaded layout

	if (mainw->stored_event_list!=NULL||mainw->sl_undo_mem!=NULL) {
	  // we are in CE mode, so event_list is in storage
	  stored_event_list_free_all(TRUE);
	}
	// in mt mode we need to do more
	else remove_current_from_affected_layouts(mainw->multitrack);

	// and we dont want to try reloading this next time
	prefs->ar_layout=FALSE;
	set_pref("ar_layout","");
	memset(prefs->ar_layout_name,0,1);
      }
      g_free(fname);
    }
    map=map_next;
  }

  // save updated layout.map
  save_layout_map(NULL,NULL,NULL,NULL);
  
}



void get_play_times(void) {
  // update the on-screen timer bars,
  // and if we are not playing,
  // get play times for video, audio channels, and total (longest) time
  char *tmpstr;
  double offset=0;
  double offset_left=0;
  double offset_right=0;
  double allocwidth;
  double allocheight;

  if (mainw->current_file==-1||mainw->foreign||cfile==NULL||mainw->multitrack!=NULL||mainw->recoverable_layout) return;

  if (mainw->playing_file==-1) {
    get_total_time (cfile);
  }

  if (!mainw->is_ready) return;

  if (mainw->laudio_drawable==NULL||mainw->raudio_drawable==NULL) return;

  // draw timer bars
  allocwidth=lives_widget_get_allocation_width(mainw->video_draw);
  allocheight=lives_widget_get_allocation_height(mainw->video_draw);

  
  if (mainw->laudio_drawable!=NULL) {
    lives_painter_t *cr=lives_painter_create(mainw->laudio_drawable);
    lives_painter_set_source_to_bg(cr,mainw->laudio_draw);

    lives_painter_rectangle(cr,0,0,
		    allocwidth,
		    allocheight);
    lives_painter_fill(cr);
    lives_painter_destroy (cr);

  }
  
  if (mainw->raudio_drawable!=NULL) {
    lives_painter_t *cr=lives_painter_create(mainw->raudio_drawable);
    lives_painter_set_source_to_bg(cr,mainw->raudio_draw);

    lives_painter_rectangle(cr,0,0,
		    allocwidth,
		    allocheight);
    lives_painter_fill(cr);
    lives_painter_destroy (cr);

  }

  if (mainw->video_drawable!=NULL) {
    lives_painter_t *cr=lives_painter_create(mainw->video_drawable);
    lives_painter_set_source_to_bg(cr,mainw->video_draw);

    lives_painter_rectangle(cr,0,0,
		    allocwidth,
		    allocheight);
    lives_painter_fill(cr);
    lives_painter_destroy (cr);

  }

  if (cfile->frames>0) {
    offset_left=(cfile->start-1)/cfile->fps/cfile->total_time*allocwidth;
    offset_right=(cfile->end)/cfile->fps/cfile->total_time*allocwidth;

    
    if (mainw->video_drawable!=NULL) {
      lives_painter_t *cr=lives_painter_create(mainw->video_drawable);
      
      lives_painter_set_source_rgb(cr, 0., 0., 0.); ///< opaque black
      
      lives_painter_rectangle(cr,0,0,
		      cfile->video_time/cfile->total_time*allocwidth-1,
		      prefs->bar_height);
      
      lives_painter_fill(cr);
      
      lives_painter_set_source_rgb(cr, 1., 1., 1.); ///< opaque white
      
      lives_painter_rectangle(cr,offset_left, 0,
		      offset_right-offset_left,
		      prefs->bar_height);
      
      lives_painter_fill(cr);
      
      lives_painter_destroy (cr);

    }
  }
  if (cfile->achans>0) {
    if (mainw->playing_file>-1) {
      offset_left=((mainw->playing_sel&&(prefs->audio_player==AUD_PLAYER_JACK||prefs->audio_player==AUD_PLAYER_PULSE))?
		   cfile->start-1.:mainw->audio_start-1.)/cfile->fps/cfile->total_time*allocwidth;
      if (mainw->audio_end&&!mainw->loop) {
	offset_right=(((prefs->audio_player==AUD_PLAYER_JACK||prefs->audio_player==AUD_PLAYER_PULSE))?
		      cfile->end:mainw->audio_end)/cfile->fps/cfile->total_time*allocwidth;
      }
      else {
	offset_right=allocwidth*cfile->laudio_time/cfile->total_time;
      }
    }
    
    if (offset_right>cfile->laudio_time/cfile->total_time*allocwidth)
      offset_right=cfile->laudio_time/cfile->total_time*allocwidth;

    
    
    if (mainw->laudio_drawable!=NULL) {
      lives_painter_t *cr=lives_painter_create(mainw->laudio_drawable);
      
      lives_painter_set_source_rgb(cr, 0., 0., 0.); ///< opaque black
      
      lives_painter_rectangle(cr,0,0,
		      cfile->laudio_time/cfile->total_time*allocwidth-1,
		      prefs->bar_height);
      
      lives_painter_fill(cr);

      if (offset_left<cfile->laudio_time/cfile->total_time*allocwidth) {
      
	lives_painter_set_source_rgb(cr, 1., 1., 1.); ///< opaque white
      
	lives_painter_rectangle(cr,offset_left, 0,
				offset_right-offset_left,
				prefs->bar_height);
	
	lives_painter_fill(cr);
      
      }

      lives_painter_destroy (cr);

    }
    if (cfile->achans>1) {
      if (mainw->raudio_drawable!=NULL) {
	lives_painter_t *cr=lives_painter_create(mainw->raudio_drawable);
      
	lives_painter_set_source_rgb(cr, 0., 0., 0.); ///< opaque black
	
	lives_painter_rectangle(cr,0,0,
			cfile->raudio_time/cfile->total_time*allocwidth-1,
			prefs->bar_height);
	
	lives_painter_fill(cr);

	if (offset_left<cfile->laudio_time/cfile->total_time*allocwidth) {
	
	  lives_painter_set_source_rgb(cr, 1., 1., 1.); ///< opaque white
	
	  lives_painter_rectangle(cr,offset_left, 0,
				  offset_right-offset_left,
				  prefs->bar_height);
      
	  lives_painter_fill(cr);

	}

	lives_painter_destroy (cr);

      }
    }
  }

  // playback cursors
  if (mainw->playing_file>-1) {
    if (cfile->frames>0) {
      offset=(mainw->actual_frame-.5)/cfile->fps;
      offset/=cfile->total_time/allocwidth;
      if (mainw->video_drawable!=NULL) {
	lives_painter_t *cr=lives_painter_create(mainw->video_drawable);

	lives_painter_set_line_width(cr,1.);
      
	if (offset>=offset_left&&offset<=offset_right) {
	  lives_painter_set_source_rgb(cr, 0., 0., 0.); ///< opaque black
	  lives_painter_move_to(cr, offset, 0);
	  lives_painter_line_to(cr, offset, prefs->bar_height);
	}
	else {
	  lives_painter_set_source_rgb(cr, 1., 1., 1.); ///< opaque white
	  lives_painter_move_to(cr, offset, 0);
	  lives_painter_line_to(cr, offset, prefs->bar_height);
	}
	lives_painter_stroke(cr);

	if (palette->style&STYLE_3||palette->style==STYLE_PLAIN) { // light style
	  lives_painter_set_source_rgb(cr, 0., 0., 0.); ///< opaque black
	  lives_painter_move_to(cr, offset, prefs->bar_height);
	  lives_painter_line_to(cr, offset, allocheight-prefs->bar_height);
	}
	else {
	  lives_painter_set_source_rgb(cr, 1., 1., 1.); ///< opaque white
	  lives_painter_move_to(cr, offset, prefs->bar_height);
	  lives_painter_line_to(cr, offset, allocheight-prefs->bar_height);
	}

	lives_painter_stroke(cr);

	lives_painter_destroy(cr);

      }
      lives_ruler_set_value(LIVES_RULER (mainw->hruler),offset*cfile->total_time/allocwidth);
      lives_widget_queue_draw (mainw->hruler);
    }
    if (cfile->achans>0&&cfile->is_loaded&&prefs->audio_src!=AUDIO_SRC_EXT) {
      if ((prefs->audio_player==AUD_PLAYER_JACK||prefs->audio_player==AUD_PLAYER_PULSE)&&
	  (mainw->event_list==NULL||!mainw->preview)) {
#ifdef ENABLE_JACK
	if (mainw->jackd!=NULL&&prefs->audio_player==AUD_PLAYER_JACK) {
	  offset=allocwidth*((double)mainw->jackd->seek_pos/cfile->arate/cfile->achans/
			     cfile->asampsize*8)/cfile->total_time;
	}
#endif
#ifdef HAVE_PULSE_AUDIO
	if (mainw->pulsed!=NULL&&prefs->audio_player==AUD_PLAYER_PULSE) {
	  offset=allocwidth*((double)mainw->pulsed->seek_pos/cfile->arate/cfile->achans/
			     cfile->asampsize*8)/cfile->total_time;
	}
#endif
      }
      else offset=allocwidth*(mainw->aframeno-.5)/cfile->fps/cfile->total_time;
      if (mainw->laudio_drawable!=NULL) {
	lives_painter_t *cr=lives_painter_create(mainw->laudio_drawable);

	lives_painter_set_line_width(cr,1.);
      
	if (offset>=offset_left&&offset<=offset_right) {
	  lives_painter_set_source_rgb(cr, 0., 0., 0.); ///< opaque black
	  lives_painter_move_to(cr, offset, 0);
	  lives_painter_line_to(cr, offset, prefs->bar_height);
	}
	else {
	  lives_painter_set_source_rgb(cr, 1., 1., 1.); ///< opaque white
	  lives_painter_move_to(cr, offset, 0);
	  lives_painter_line_to(cr, offset, prefs->bar_height);
	}
	lives_painter_stroke(cr);

	if (palette->style&STYLE_3||palette->style==STYLE_PLAIN) { // light style
	  lives_painter_set_source_rgb(cr, 0., 0., 0.); ///< opaque black
	  lives_painter_move_to(cr, offset, prefs->bar_height);
	  lives_painter_line_to(cr, offset, allocheight-prefs->bar_height);
	}
	else {
	  lives_painter_set_source_rgb(cr, 1., 1., 1.); ///< opaque white
	  lives_painter_move_to(cr, offset, prefs->bar_height);
	  lives_painter_line_to(cr, offset, allocheight-prefs->bar_height);
	}
	lives_painter_stroke(cr);

	lives_painter_destroy(cr);
      }

      if (cfile->achans>1) {
	if (mainw->raudio_drawable!=NULL) {
	  lives_painter_t *cr=lives_painter_create(mainw->raudio_drawable);
	  
	  lives_painter_set_line_width(cr,1.);
	  
	  if (offset>=offset_left&&offset<=offset_right) {
	    lives_painter_set_source_rgb(cr, 0., 0., 0.); ///< opaque black
	    lives_painter_move_to(cr, offset, 0);
	    lives_painter_line_to(cr, offset, prefs->bar_height);
	  }
	  else {
	    lives_painter_set_source_rgb(cr, 1., 1., 1.); ///< opaque white
	    lives_painter_move_to(cr, offset, 0);
	    lives_painter_line_to(cr, offset, prefs->bar_height);
	  }
	  lives_painter_stroke(cr);
	  
	  if (palette->style&STYLE_3||palette->style==STYLE_PLAIN) { // light style
	    lives_painter_set_source_rgb(cr, 0., 0., 0.); ///< opaque black
	    lives_painter_move_to(cr, offset, prefs->bar_height);
	    lives_painter_line_to(cr, offset, allocheight-prefs->bar_height);
	  }
	  else {
	    lives_painter_set_source_rgb(cr, 1., 1., 1.); ///< opaque white
	    lives_painter_move_to(cr, offset, prefs->bar_height);
	    lives_painter_line_to(cr, offset, allocheight-prefs->bar_height);
	  }
	  lives_painter_stroke(cr);
	  
	  lives_painter_destroy(cr);

	}
      }
    }
    if (cfile->frames==0) {
      lives_ruler_set_value(LIVES_RULER (mainw->hruler),offset*cfile->total_time/allocwidth);
      lives_widget_queue_draw (mainw->hruler);
    }
  }
  
  if (mainw->playing_file==-1||(mainw->switch_during_pb&&!mainw->faded)) {
    if (cfile->total_time>0.) {
      // set the range of the timeline
      if (!cfile->opening_loc) {
	lives_widget_show (mainw->hruler);
      }
      lives_widget_show (mainw->eventbox5);
      lives_widget_show (mainw->video_draw);
      lives_widget_show (mainw->laudio_draw);
      lives_widget_show (mainw->raudio_draw);

      lives_ruler_set_upper(LIVES_RULER (mainw->hruler),cfile->total_time);
      lives_widget_queue_draw(mainw->hruler);

      draw_little_bars(cfile->pointer_time);

      if (mainw->playing_file==-1&&mainw->play_window!=NULL&&cfile->is_loaded) {
	if (mainw->preview_box==NULL) {
	  // create the preview box that shows frames
	  make_preview_box();
	}
	// and add it the play window
	if (lives_widget_get_parent(mainw->preview_box)==NULL&&(cfile->clip_type==CLIP_TYPE_DISK||
					       cfile->clip_type==CLIP_TYPE_FILE)&&!mainw->is_rendering) {
	  lives_widget_queue_draw(mainw->play_window);
	  lives_container_add (LIVES_CONTAINER (mainw->play_window), mainw->preview_box);
	  lives_widget_grab_focus (mainw->preview_spinbutton);
	  play_window_set_title();
	  load_preview_image(FALSE);
	}
      }
    }
    else {
      lives_widget_hide (mainw->hruler);
      lives_widget_hide (mainw->eventbox5);
    }

    if (cfile->opening_loc||(cfile->frames==123456789&&cfile->opening)) {
      tmpstr=g_strdup(_ ("Video [opening...]"));
    }
    else {
      if (cfile->video_time>0.) {
	tmpstr=g_strdup_printf(_ ("Video [%.2f sec]"),cfile->video_time);
      }
      else {
	if (cfile->video_time<=0.&&cfile->frames>0) {
	  tmpstr=g_strdup (_ ("(Undefined)"));
	}
	else {
	  tmpstr=g_strdup (_ ("(No video)"));
	}
      }
    }
    lives_label_set_text(LIVES_LABEL(mainw->vidbar),tmpstr);
    g_free(tmpstr);
    if (cfile->achans==0) {
      tmpstr=g_strdup (_ ("(No audio)"));
    }
    else {
      if (cfile->opening_audio) {
	if (cfile->achans==1) {
	  tmpstr=g_strdup (_ ("Mono  [opening...]"));
	}
	else {
	  tmpstr=g_strdup (_ ("Left Audio [opening...]"));
	}
      }
      else {
	if (cfile->achans==1) {
	  tmpstr=g_strdup_printf(_ ("Mono [%.2f sec]"),cfile->laudio_time);
	}
	else {
	  tmpstr=g_strdup_printf(_ ("Left Audio [%.2f sec]"),cfile->laudio_time);
	}
      }
    }
    lives_label_set_text(LIVES_LABEL(mainw->laudbar),tmpstr);
    g_free(tmpstr);
    if (cfile->achans>1) {
      if (cfile->opening_audio) {
	tmpstr=g_strdup (_ ("Right Audio [opening...]"));
      }
      else {
	tmpstr=g_strdup_printf(_ ("Right Audio [%.2f sec]"),cfile->raudio_time);
      }
      lives_label_set_text(LIVES_LABEL(mainw->raudbar),tmpstr);
      lives_widget_show (mainw->raudbar);
      g_free(tmpstr);
    }
    else {
      lives_widget_hide (mainw->raudbar);
    }
  }
  else {
    double ptrtime=(mainw->actual_frame-.5)/cfile->fps;
    if (ptrtime<0.) ptrtime=0.;
    draw_little_bars(ptrtime);
  }

  if (!mainw->draw_blocked) {
    lives_widget_queue_draw(mainw->video_draw);
    lives_widget_queue_draw(mainw->laudio_draw);
    lives_widget_queue_draw(mainw->raudio_draw);
  }
  lives_widget_queue_draw(mainw->vidbar);
  lives_widget_queue_draw(mainw->hruler);
}
    

void draw_little_bars (double ptrtime) {
  //draw the vertical player bars
  double allocheight=lives_widget_get_allocation_height(mainw->video_draw)-prefs->bar_height;
  double offset=ptrtime/cfile->total_time*lives_widget_get_allocation_width(mainw->vidbar);
  int frame;

  if (!prefs->show_gui) return;

  if (!(frame=calc_frame_from_time(mainw->current_file,ptrtime)))
   frame=cfile->frames;

  if (cfile->frames>0) {
    if (mainw->video_drawable!=NULL) {
      lives_painter_t *cr=lives_painter_create(mainw->video_drawable);

      lives_painter_set_line_width(cr,1.);
      
      if (frame>=cfile->start&&frame<=cfile->end) {
	lives_painter_set_source_rgb(cr, 0., 0., 0.); ///< opaque black
	lives_painter_move_to(cr, offset, 0);
	lives_painter_line_to(cr, offset, prefs->bar_height);
      }
      else {
	lives_painter_set_source_rgb(cr, 1., 1., 1.); ///< opaque white
	lives_painter_move_to(cr, offset, 0);
	lives_painter_line_to(cr, offset, prefs->bar_height);
      }
      lives_painter_stroke(cr);

      if (palette->style&STYLE_3||palette->style==STYLE_PLAIN) { // light style
	lives_painter_set_source_rgb(cr, 0., 0., 0.); ///< opaque black
	lives_painter_move_to(cr, offset, prefs->bar_height);
	lives_painter_line_to(cr, offset, allocheight-prefs->bar_height);
      }
      else {
	lives_painter_set_source_rgb(cr, 1., 1., 1.); ///< opaque white
	lives_painter_move_to(cr, offset, prefs->bar_height);
	lives_painter_line_to(cr, offset, allocheight-prefs->bar_height);
      }
      lives_painter_stroke(cr);

      lives_painter_destroy(cr);


    }
  }

  if (mainw->playing_file>-1) return;

  if (cfile->achans>0) {
    if (mainw->laudio_drawable!=NULL) {
      lives_painter_t *cr=lives_painter_create(mainw->laudio_drawable);

      lives_painter_set_line_width(cr,1.);
      
      if (frame>=cfile->start&&frame<=cfile->end) {
	lives_painter_set_source_rgb(cr, 0., 0., 0.); ///< opaque black
	lives_painter_move_to(cr, offset, 0);
	lives_painter_line_to(cr, offset, prefs->bar_height);
      }
      else {
	lives_painter_set_source_rgb(cr, 1., 1., 1.); ///< opaque white
	lives_painter_move_to(cr, offset, 0);
	lives_painter_line_to(cr, offset, prefs->bar_height);
      }
      lives_painter_stroke(cr);

      if (palette->style&STYLE_3||palette->style==STYLE_PLAIN) { // light style
	lives_painter_set_source_rgb(cr, 0., 0., 0.); ///< opaque black
	lives_painter_move_to(cr, offset, prefs->bar_height);
	lives_painter_line_to(cr, offset, allocheight-prefs->bar_height);
      }
      else {
	lives_painter_set_source_rgb(cr, 1., 1., 1.); ///< opaque white
	lives_painter_move_to(cr, offset, prefs->bar_height);
	lives_painter_line_to(cr, offset, allocheight-prefs->bar_height);
      }
      lives_painter_stroke(cr);

      lives_painter_destroy(cr);

    }

    if (cfile->achans>1) {
      if (mainw->raudio_drawable!=NULL) {
	if ((frame>=cfile->start&&frame<=cfile->end)||mainw->loop) {
	lives_painter_t *cr=lives_painter_create(mainw->raudio_drawable);

	lives_painter_set_line_width(cr,1.);
      
	if (frame>=cfile->start&&frame<=cfile->end) {
	  lives_painter_set_source_rgb(cr, 0., 0., 0.); ///< opaque black
	  lives_painter_move_to(cr, offset, 0);
	  lives_painter_line_to(cr, offset, prefs->bar_height);
	}
	else {
	  lives_painter_set_source_rgb(cr, 1., 1., 1.); ///< opaque white
	  lives_painter_move_to(cr, offset, 0);
	  lives_painter_line_to(cr, offset, prefs->bar_height);
	}
	lives_painter_stroke(cr);

	if (palette->style&STYLE_3||palette->style==STYLE_PLAIN) { // light style
	  lives_painter_set_source_rgb(cr, 0., 0., 0.); ///< opaque black
	  lives_painter_move_to(cr, offset, prefs->bar_height);
	  lives_painter_line_to(cr, offset, allocheight-prefs->bar_height);
	}
	else {
	  lives_painter_set_source_rgb(cr, 1., 1., 1.); ///< opaque white
	  lives_painter_move_to(cr, offset, prefs->bar_height);
	  lives_painter_line_to(cr, offset, allocheight-prefs->bar_height);
	}
	lives_painter_stroke(cr);

	lives_painter_destroy(cr);

	}
      }
    }
  }
  threaded_dialog_spin();
}




void get_total_time (file *file) {
  // get times (video, left and right audio)

  file->laudio_time=file->raudio_time=file->video_time=file->total_time=0.;

  if (file->opening&&file->frames!=123456789) {
    if (file->frames*file->fps>0) {
      file->video_time=file->total_time=file->frames/file->fps;
    }
    return;
  }

  if (file->fps>0.) {
    file->total_time=file->video_time=file->frames/file->fps;
  }

  if (file->asampsize*file->arate*file->achans) {
    file->laudio_time=(double)(file->afilesize/(file->asampsize>>3)/file->achans)/(double)file->arate;
    if (file->achans>1) {
      file->raudio_time=file->laudio_time;
    }
  }

  if (file->laudio_time>file->total_time) file->total_time=file->laudio_time;
  if (file->raudio_time>file->total_time) file->total_time=file->raudio_time;

  if (file->laudio_time+file->raudio_time==0.&&!file->opening) {
    file->achans=file->afilesize=file->asampsize=file->arate=file->arps=0;
  }
}



void 
find_when_to_stop (void) {
  // work out when to stop playing
  //
  // ---------------
  //        no loop              loop to fit                 loop cont
  //        -------              -----------                 ---------
  // a>v    stop on video end    stop on audio end           no stop
  // v>a    stop on video end    stop on video end           no stop
  // generator start - not playing : stop on vid_end, unless pure audio;
  if (cfile->clip_type==CLIP_TYPE_GENERATOR||mainw->aud_rec_fd!=-1) mainw->whentostop=STOP_ON_VID_END;
  else if (mainw->multitrack!=NULL&&cfile->frames>0) mainw->whentostop=STOP_ON_VID_END;
  else if (cfile->clip_type!=CLIP_TYPE_DISK&&cfile->clip_type!=CLIP_TYPE_FILE) mainw->whentostop=NEVER_STOP;
  else if (cfile->opening_only_audio) mainw->whentostop=STOP_ON_AUD_END;
  else if (cfile->opening_audio) mainw->whentostop=STOP_ON_VID_END;
  else if (!mainw->preview&&(mainw->loop_cont||(mainw->loop&&prefs->audio_src==AUDIO_SRC_EXT))) mainw->whentostop=NEVER_STOP;
  else if (cfile->frames==0||(mainw->loop&&cfile->achans>0&&!mainw->is_rendering&&(mainw->audio_end/cfile->fps)
			      <MAX (cfile->laudio_time,cfile->raudio_time)&&
			      calc_time_from_frame(mainw->current_file,mainw->play_start)<cfile->laudio_time)) 
    mainw->whentostop=STOP_ON_AUD_END;
  else mainw->whentostop=STOP_ON_VID_END; // tada...
}


#define ASPECT_ALLOWANCE 0.005


void 
minimise_aspect_delta (double aspect,int hblock,int vblock,int hsize,int vsize,int *width,int *height) {
  // we will use trigonometry to calculate the smallest difference between a given
  // aspect ratio and the actual frame size. If the delta is smaller than current 
  // we set the height and width
  int cw=width[0];
  int ch=height[0];

  int real_width,real_height;
  uint64_t delta,current_delta;

  // minimise d[(x-x1)^2 + (y-y1)^2]/d[x1], to get approximate values
  int calc_width=(int)((vsize+aspect*hsize)*aspect/(aspect*aspect+1.));

  int i;

  current_delta=(hsize-cw)*(hsize-cw)+(vsize-ch)*(vsize-ch);

#ifdef DEBUG_ASPECT
  g_printerr ("aspect %.8f : width %d height %d is best fit\n",aspect,calc_width,(int)(calc_width/aspect));
#endif
  // use the block size to find the nearest allowed size
  for (i=-1;i<2;i++) {
    real_width=(int)(calc_width/hblock+i)*hblock;
    real_height=(int)(real_width/aspect/vblock+.5)*vblock;
    delta=(hsize-real_width)*(hsize-real_width)+(vsize-real_height)*(vsize-real_height);


    if (real_width%hblock!=0||real_height%vblock!=0||ABS((double)real_width/(double)real_height-aspect)>ASPECT_ALLOWANCE) {
      // encoders can be fussy, so we need to fit both aspect ratio and blocksize      
      while (1) {
	real_width=((int)(real_width/hblock)+1)*hblock;
	real_height=(int)((double)real_width/aspect+.5);
	
	if (real_height%vblock==0) break;
	
	real_height=((int)(real_height/vblock)+1)*vblock;
	real_width=(int)((double)real_height*aspect+.5);
	
	if (real_width%hblock==0) break;
	
      }
    }

#ifdef DEBUG_ASPECT
    g_printerr ("block quantise to %d x %d\n",real_width,real_height);
#endif
    if (delta<current_delta) {
#ifdef DEBUG_ASPECT
      g_printerr ("is better fit\n");
#endif
      current_delta=delta;
      width[0]=real_width;
      height[0]=real_height;
    }
  }
}

void zero_spinbuttons (void) {
  g_signal_handler_block(mainw->spinbutton_start,mainw->spin_start_func);
  lives_spin_button_set_range(LIVES_SPIN_BUTTON(mainw->spinbutton_start),0.,0.);
  lives_spin_button_set_value(LIVES_SPIN_BUTTON(mainw->spinbutton_start),0.);
  g_signal_handler_unblock(mainw->spinbutton_start,mainw->spin_start_func);
  g_signal_handler_block(mainw->spinbutton_end,mainw->spin_end_func);
  lives_spin_button_set_range(LIVES_SPIN_BUTTON(mainw->spinbutton_end),0.,0.);
  lives_spin_button_set_value(LIVES_SPIN_BUTTON(mainw->spinbutton_end),0.);
  g_signal_handler_unblock(mainw->spinbutton_end,mainw->spin_end_func);
}




boolean switch_aud_to_jack(void) {
#ifdef ENABLE_JACK
  if (mainw->is_ready) {
    lives_jack_init();
    if (mainw->jackd==NULL) {
      jack_audio_init();
      jack_audio_read_init();
      mainw->jackd=jack_get_driver(0,TRUE);
      if (jack_open_device(mainw->jackd)) {
	mainw->jackd=NULL;
	return FALSE;
      }
      mainw->jackd->whentostop=&mainw->whentostop;
      mainw->jackd->cancelled=&mainw->cancelled;
      mainw->jackd->in_use=FALSE;
      mainw->jackd->play_when_stopped=(prefs->jack_opts&JACK_OPTS_NOPLAY_WHEN_PAUSED)?FALSE:TRUE;
      jack_driver_activate(mainw->jackd);
    }
    mainw->aplayer_broken=FALSE;
    lives_widget_show(mainw->vol_toolitem);
    if (mainw->vol_label!=NULL) lives_widget_show(mainw->vol_label);
    lives_widget_show (mainw->recaudio_submenu);

    if (mainw->vpp!=NULL&&mainw->vpp->get_audio_fmts!=NULL) 
      mainw->vpp->audio_codec=get_best_audio(mainw->vpp);

#ifdef HAVE_PULSE_AUDIO
    if (mainw->pulsed_read!=NULL) {
      pulse_close_client(mainw->pulsed_read);
      mainw->pulsed_read=NULL;
    }
    
    if (mainw->pulsed!=NULL) {
      pulse_close_client(mainw->pulsed);
      mainw->pulsed=NULL;
      pulse_shutdown();
    }
#endif
  }
  prefs->audio_player=AUD_PLAYER_JACK;
  set_pref("audio_player","jack");
  g_snprintf(prefs->aplayer,512,"%s","jack");

  if (mainw->is_ready&&mainw->vpp!=NULL&&mainw->vpp->get_audio_fmts!=NULL) 
    mainw->vpp->audio_codec=get_best_audio(mainw->vpp);

  return TRUE;
#endif
  return FALSE;
}



boolean switch_aud_to_pulse(void) {
#ifdef HAVE_PULSE_AUDIO
  boolean retval;

  if (mainw->is_ready) {
    if ((retval=lives_pulse_init(-1))) {
      if (mainw->pulsed==NULL) {
	pulse_audio_init();
	pulse_audio_read_init();
	mainw->pulsed=pulse_get_driver(TRUE);
	mainw->pulsed->whentostop=&mainw->whentostop;
	mainw->pulsed->cancelled=&mainw->cancelled;
	mainw->pulsed->in_use=FALSE;
	pulse_driver_activate(mainw->pulsed);
      }
      mainw->aplayer_broken=FALSE;
      lives_widget_show(mainw->vol_toolitem);
      if (mainw->vol_label!=NULL) lives_widget_show(mainw->vol_label);
      lives_widget_show (mainw->recaudio_submenu);

      prefs->audio_player=AUD_PLAYER_PULSE;
      set_pref("audio_player","pulse");
      g_snprintf(prefs->aplayer,512,"%s","pulse");

      if (mainw->vpp!=NULL&&mainw->vpp->get_audio_fmts!=NULL) 
	mainw->vpp->audio_codec=get_best_audio(mainw->vpp);

    }
    
#ifdef ENABLE_JACK
    if (mainw->jackd_read!=NULL) {
      jack_close_device(mainw->jackd_read);
      mainw->jackd_read=NULL;
    }

    if (mainw->jackd!=NULL) {
      jack_close_device(mainw->jackd);
      mainw->jackd=NULL;
    }
#endif
    return retval;
  }

#endif
  return FALSE;
}



void switch_aud_to_sox(boolean set_in_prefs) {
  prefs->audio_player=AUD_PLAYER_SOX;
  get_pref_default("sox_command",prefs->audio_play_command,256);
  if (set_in_prefs) set_pref("audio_player","sox");
  g_snprintf(prefs->aplayer,512,"%s","sox");
  set_pref("audio_play_command",prefs->audio_play_command);
  if (mainw->is_ready) {
    lives_widget_hide(mainw->vol_toolitem);
    if (mainw->vol_label!=NULL) lives_widget_hide(mainw->vol_label);
    lives_widget_hide (mainw->recaudio_submenu);
    
    if (mainw->vpp!=NULL&&mainw->vpp->get_audio_fmts!=NULL) 
      mainw->vpp->audio_codec=get_best_audio(mainw->vpp);
  }

#ifdef ENABLE_JACK
  if (mainw->jackd_read!=NULL) {
    jack_close_device(mainw->jackd_read);
    mainw->jackd_read=NULL;
  }

  if (mainw->jackd!=NULL) {
    jack_close_device(mainw->jackd);
    mainw->jackd=NULL;
  }
#endif

#ifdef HAVE_PULSE_AUDIO
  if (mainw->pulsed_read!=NULL) {
    pulse_close_client(mainw->pulsed_read);
    mainw->pulsed_read=NULL;
  }

  if (mainw->pulsed!=NULL) {
    pulse_close_client(mainw->pulsed);
    mainw->pulsed=NULL;
    pulse_shutdown();
  }
#endif

}



void switch_aud_to_mplayer(boolean set_in_prefs) {
  int i;
  for (i=1;i<MAX_FILES;i++) {
    if (mainw->files[i]!=NULL) {
      if (i!=mainw->current_file&&mainw->files[i]->opening) {
	do_error_dialog(_ ("LiVES cannot switch to mplayer whilst clips are loading."));
	return;
      }
    }
  }
  
  prefs->audio_player=AUD_PLAYER_MPLAYER;
  get_pref_default("mplayer_audio_command",prefs->audio_play_command,256);
  if (set_in_prefs) set_pref("audio_player","mplayer");
  g_snprintf(prefs->aplayer,512,"%s","mplayer");
  set_pref("audio_play_command",prefs->audio_play_command);
  if (mainw->is_ready) {
    lives_widget_hide(mainw->vol_toolitem);
    if (mainw->vol_label!=NULL) lives_widget_hide(mainw->vol_label);
    lives_widget_hide (mainw->recaudio_submenu);

    if (mainw->vpp!=NULL&&mainw->vpp->get_audio_fmts!=NULL) 
      mainw->vpp->audio_codec=get_best_audio(mainw->vpp);
  }

#ifdef ENABLE_JACK
  if (mainw->jackd_read!=NULL) {
    jack_close_device(mainw->jackd_read);
    mainw->jackd_read=NULL;
  }

  if (mainw->jackd!=NULL) {
    jack_close_device(mainw->jackd);
    mainw->jackd=NULL;
  }
#endif

#ifdef HAVE_PULSE_AUDIO
  if (mainw->pulsed_read!=NULL) {
    pulse_close_client(mainw->pulsed_read);
    mainw->pulsed_read=NULL;
  }

  if (mainw->pulsed!=NULL) {
    pulse_close_client(mainw->pulsed);
    mainw->pulsed=NULL;
    pulse_shutdown();
  }
#endif

}



boolean prepare_to_play_foreign(void) {
  // here we are going to 'play' a captured external window

#ifdef GUI_GTK



#if !GTK_CHECK_VERSION(3,0,0)
#ifdef GDK_WINDOWING_X11
  GdkVisual *vissi=NULL;
  register int i;
#endif
#endif
#endif

  int new_file=mainw->first_free_file;

  mainw->foreign_window=NULL;

  // create a new 'file' to play into
  if (!get_new_handle(new_file,NULL)) {
    return FALSE;
  }

  mainw->current_file=new_file;

  if (mainw->rec_achans>0) {
    cfile->arate=cfile->arps=mainw->rec_arate;
    cfile->achans=mainw->rec_achans;
    cfile->asampsize=mainw->rec_asamps;
    cfile->signed_endian=mainw->rec_signed_endian;
#ifdef HAVE_PULSE_AUDIO
    if (mainw->rec_achans>0&&prefs->audio_player==AUD_PLAYER_PULSE&&mainw->pulsed_read==NULL) {
      lives_pulse_init(0);
      pulse_audio_read_init();
      pulse_rec_audio_to_clip(mainw->current_file,-1,RECA_WINDOW_GRAB);
    }
#endif
#ifdef ENABLE_JACK
    if (mainw->rec_achans>0&&prefs->audio_player==AUD_PLAYER_JACK&&mainw->jackd_read==NULL) 
      jack_rec_audio_to_clip(mainw->current_file,-1,RECA_WINDOW_GRAB);
#endif
  }

  cfile->hsize=mainw->foreign_width/2+1;
  cfile->vsize=mainw->foreign_height/2+3;

  cfile->fps=cfile->pb_fps=mainw->rec_fps;

  resize(-2);

  lives_widget_show (mainw->playframe);
  lives_widget_show (mainw->playarea);
  lives_widget_context_update();

  // size must be exact, must not be larger than play window or we end up with nothing
  mainw->pwidth=lives_widget_get_allocation_width(mainw->playframe)-H_RESIZE_ADJUST+2;
  mainw->pheight=lives_widget_get_allocation_height(mainw->playframe)-V_RESIZE_ADJUST+2;

  cfile->hsize=mainw->pwidth;
  cfile->vsize=mainw->pheight;

#ifdef GUI_GTK
#if GTK_CHECK_VERSION(3,0,0)

#ifdef GDK_WINDOWING_X11
  mainw->foreign_window=gdk_x11_window_foreign_new_for_display
    (mainw->mgeom[prefs->gui_monitor>0?prefs->gui_monitor-1:0].disp,
     mainw->foreign_id);
#else
#ifdef GDK_WINDOWING_WIN32
  if (mainw->foreign_window==NULL)
    mainw->foreign_window=gdk_win32_window_foreign_new_for_display
      (mainw->mgeom[prefs->gui_monitor>0?prefs->gui_monitor-1:0].disp,
       mainw->foreign_id);
#endif

#endif // GDK_WINDOWING

  if (mainw->foreign_window!=NULL) gdk_window_set_keep_above(mainw->foreign_window,TRUE);

#else // 3,0,0
  mainw->foreign_window=gdk_window_foreign_new(mainw->foreign_id);
#endif
#endif // GUI_GTK

#ifdef GUI_GTK
#ifdef GDK_WINDOWING_X11
#if !GTK_CHECK_VERSION(3,0,0)

  if (mainw->foreign_visual!=NULL) {
    for (i=0;i<capable->nmonitors;i++) {
      vissi=gdk_x11_screen_lookup_visual(mainw->mgeom[i].screen,hextodec(mainw->foreign_visual));
      if (vissi!=NULL) break;
    }
  }

  if (vissi==NULL) vissi=gdk_visual_get_best_with_depth (mainw->foreign_bpp);

  if (vissi==NULL) return FALSE;

  mainw->foreign_cmap=gdk_x11_colormap_foreign_new(vissi, 
						   gdk_x11_colormap_get_xcolormap(gdk_colormap_new(vissi,TRUE)));

  if (mainw->foreign_cmap==NULL) return FALSE;

#endif
#endif
#endif

  if (mainw->foreign_window==NULL) return FALSE;

  mainw->play_start=1;
  if (mainw->rec_vid_frames==-1) mainw->play_end=INT_MAX;
  else mainw->play_end=mainw->rec_vid_frames;

  mainw->rec_samples=-1;

  omute=mainw->mute;
  osepwin=mainw->sep_win;
  ofs=mainw->fs;
  ofaded=mainw->faded;
  odouble=mainw->double_size;

  mainw->mute=TRUE;
  mainw->sep_win=FALSE;
  mainw->fs=FALSE;
  mainw->faded=TRUE;
  mainw->double_size=FALSE;

  lives_widget_hide(mainw->t_double);
  lives_widget_hide(mainw->t_bckground);
  lives_widget_hide(mainw->t_sepwin);
  lives_widget_hide(mainw->t_infobutton);

  return TRUE;
}


boolean after_foreign_play(void) {
  // read details from capture file
  int capture_fd;
  char *capfile=g_strdup_printf("%s/.capture.%d",prefs->tmpdir,getpid());
  char capbuf[256];
  ssize_t length;
  int new_file=-1;
  int new_frames=0;
  int old_file=mainw->current_file;

  char *com;
  char **array;
  char file_name[PATH_MAX];

  // assume for now we only get one clip passed back
  if ((capture_fd=open(capfile,O_RDONLY))>-1) {
    memset(capbuf,0,256);
    if ((length=read(capture_fd,capbuf,256))) {
      if (get_token_count (capbuf,'|')>2) {
	array=g_strsplit(capbuf,"|",3);
	new_frames=atoi(array[1]);

	if (new_frames>0) {
	  new_file=mainw->first_free_file;
	  mainw->current_file=new_file;
	  cfile=(file *)(g_malloc(sizeof(file)));
	  g_snprintf(cfile->handle,255,"%s",array[0]);
	  g_strfreev(array);
	  create_cfile();
	  g_snprintf(cfile->file_name,256,"Capture %d",mainw->cap_number);
	  g_snprintf(cfile->name,256,"Capture %d",mainw->cap_number++);
	  g_snprintf(cfile->type,40,"Frames");

	  cfile->progress_start=cfile->start=1;
	  cfile->progress_end=cfile->frames=cfile->end=new_frames;
	  cfile->pb_fps=cfile->fps=mainw->rec_fps;

	  cfile->hsize=CEIL(mainw->foreign_width,4);
	  cfile->vsize=CEIL(mainw->foreign_height,4);
	  
	  if (mainw->rec_achans>0) {
	    cfile->arate=cfile->arps=mainw->rec_arate;
	    cfile->achans=mainw->rec_achans;
	    cfile->asampsize=mainw->rec_asamps;
	    cfile->signed_endian=mainw->rec_signed_endian;
	  }
	  
	  // TODO - dirsep

	  g_snprintf(file_name,PATH_MAX,"%s/%s/",prefs->tmpdir,cfile->handle);
	  
	  com=g_strdup_printf("%s fill_and_redo_frames \"%s\" %d %d %d \"%s\" %.4f %d %d %d %d %d",
			      prefs->backend,
			      cfile->handle,cfile->frames,cfile->hsize,cfile->vsize,
			      get_image_ext_for_type(cfile->img_type),cfile->fps,cfile->arate,
			      cfile->achans,cfile->asampsize,!(cfile->signed_endian&AFORM_UNSIGNED),
			      !(cfile->signed_endian&AFORM_BIG_ENDIAN));
	  unlink(cfile->info_file);
	  mainw->com_failed=FALSE;
	  lives_system(com,FALSE);


	  cfile->nopreview=TRUE;
	  if (!mainw->com_failed&&do_progress_dialog(TRUE,TRUE,_ ("Cleaning up clip"))) {
	    get_next_free_file();
	  }
	  else {
	    // cancelled cleanup
	    new_frames=0;
	    close_current_file(old_file);
	  }
	  g_free(com);
	}
	else g_strfreev(array);
      }
      close(capture_fd);
      unlink(capfile);
    }
  }

  if (new_frames==0) {
    // nothing captured; or cancelled
    g_free(capfile);
    return FALSE;
  }

  cfile->nopreview=FALSE;
  g_free(capfile);

  add_to_winmenu();
  if (mainw->multitrack==NULL) switch_to_file(old_file,mainw->current_file);
  
  else {
    mainw->current_file=mainw->multitrack->render_file;
    mt_init_clips(mainw->multitrack,new_file,TRUE);
    mt_clip_select(mainw->multitrack,TRUE);
  }

  cfile->is_loaded=TRUE;
  cfile->changed=TRUE;
  save_clip_values(mainw->current_file);
  if (prefs->crash_recovery) add_to_recovery_file(cfile->handle);
#ifdef ENABLE_OSC
  lives_osc_notify(LIVES_OSC_NOTIFY_CLIP_OPENED,"");
#endif
  return TRUE;
}


void set_menu_text(GtkWidget *menuitem, const char *text, boolean use_mnemonic) {
  GtkWidget *label;
  if (GTK_IS_MENU_ITEM (menuitem)) {
    label=lives_bin_get_child(GTK_BIN(menuitem));
    if (use_mnemonic) {
      lives_label_set_text_with_mnemonic(LIVES_LABEL(label),text);
    }
    else {
      lives_label_set_text(LIVES_LABEL(label),text);
    }
  }
}


void get_menu_text(GtkWidget *menuitem, char *text) {
  GtkWidget *label=lives_bin_get_child(GTK_BIN(menuitem));
  g_snprintf(text,255,"%s",gtk_label_get_text(LIVES_LABEL(label)));
}

void
get_menu_text_long(GtkWidget *menuitem, char *text) {
  GtkWidget *label=lives_bin_get_child(GTK_BIN(menuitem));
  g_snprintf(text,32768,"%s",gtk_label_get_text(LIVES_LABEL(label)));
}


LIVES_INLINE boolean int_array_contains_value(int *array, int num_elems, int value) {
  int i;
  for (i=0;i<num_elems;i++) {
    if (array[i]==value) return TRUE;
  }
  return FALSE;
}


void 
reset_clip_menu (void) {
  // sometimes the clip menu gets messed up, e.g. after reloading a set.
  // This function will clean up the 'x's and so on.

  int i;
  GtkWidget *active_image=NULL;
  char menutext[32768];

  for (i=1;i<=MAX_FILES;i++) {
    if (!(mainw->files[i]==NULL)) {
      if (!(active_image==NULL)) {
	active_image=NULL;
      }
      if (mainw->files[i]->opening) {
	active_image = lives_image_new_from_stock ("gtk-no", LIVES_ICON_SIZE_MENU);
      }
      else {
	if (i==mainw->current_file) {
	  active_image = lives_image_new_from_stock ("gtk-close", LIVES_ICON_SIZE_MENU);
	}
      }
      if (!(active_image==NULL)) {
	lives_widget_show (active_image);
      }
      if (mainw->files[i]->menuentry!=NULL) {
	get_menu_text_long(mainw->files[i]->menuentry,menutext);
	lives_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (mainw->files[i]->menuentry), active_image);
	set_menu_text(mainw->files[i]->menuentry,menutext,FALSE);
	lives_widget_queue_draw(mainw->files[i]->menuentry);
      }
    }
  }
}



boolean check_file(const char *file_name, boolean check_existing) {
  int check;
  boolean exists=FALSE;
  char *msg;
  // file_name should be in utf8
  char *lfile_name=g_filename_from_utf8(file_name,-1,NULL,NULL,NULL);

  // check if file exists
  if (g_file_test (lfile_name, G_FILE_TEST_EXISTS)) {
    if (check_existing) {
      msg=g_strdup_printf (_ ("\n%s\nalready exists.\n\nOverwrite ?\n"),file_name);
      if (!do_warning_dialog(msg)) {
	g_free (msg);
	g_free(lfile_name);
	return FALSE;
      }
      g_free (msg);
    }
    check=open(lfile_name,O_WRONLY);
    exists=TRUE;
  }
  // if not, check if we can write to it
  else {
    check=open(lfile_name,O_CREAT|O_EXCL|O_WRONLY,DEF_FILE_PERMS);
  }

  if (check<0) {
    if (mainw!=NULL&&mainw->is_ready) {
      if (errno==EACCES)
	do_file_perm_error(lfile_name);
      else 
	do_write_failed_error_s(lfile_name,NULL);
    }
    g_free(lfile_name);
    return FALSE;
  }

  close(check);
  if (!exists) {
    unlink (lfile_name);
  }
  g_free(lfile_name);
  return TRUE;
}



boolean check_dir_access (const char *dir) {
  // if a directory exists, make sure it is readable and writable
  // otherwise create it and then check

  // dir is in locale encoding

  // see also is_writeable_dir() which uses statvfs

  boolean exists=g_file_test (dir, G_FILE_TEST_EXISTS);
  boolean is_OK=FALSE;

  char *com;
  char *testfile;

  if (!exists) {
    g_mkdir_with_parents(dir,S_IRWXU);
  }

  if (!g_file_test(dir, G_FILE_TEST_IS_DIR)) return FALSE;

  testfile=g_build_filename (dir,"livestst.txt",NULL);
#ifndef IS_MINGW
  com=g_strdup_printf ("/bin/touch \"%s\"",testfile);
#else
  com=g_strdup_printf ("touch.exe \"%s\"",testfile);
#endif
  lives_system (com,TRUE);
  g_free (com);
  if ((is_OK=g_file_test(testfile, G_FILE_TEST_EXISTS))) {
    unlink (testfile);
  }
  g_free (testfile);
  if (!exists) {
    rmdir(dir);
  }
  return is_OK;
}


boolean check_dev_busy(char *devstr) {
#ifndef IS_MINGW
  int ret;
#ifdef IS_SOLARIS
  struct flock lock;
  lock.l_start = 0;
  lock.l_whence = SEEK_SET;
  lock.l_len = 0;
  lock.l_type = F_WRLCK;
#endif
  int fd=open(devstr,O_RDONLY|O_NONBLOCK);
  if (fd==-1) return FALSE;
#ifdef IS_SOLARIS
  ret=fcntl(fd, F_SETLK, &lock);
#else
  ret=flock(fd,LOCK_EX|LOCK_NB);
#endif
  close(fd);
  if (ret==-1) return FALSE;
  return TRUE;
#endif
  return FALSE;
}



void activate_url_inner(const char *link) {
#if GTK_CHECK_VERSION(2,14,0)
  GError *err=NULL;
  gtk_show_uri(NULL,link,GDK_CURRENT_TIME,&err);
#else
  char *com = getenv("BROWSER");
  com = g_strdup_printf("\"%s\" '%s' &", com ? com : "gnome-open", link);
  lives_system(com,FALSE);
  g_free(com);
#endif
}


void activate_url (GtkAboutDialog *about, const char *link, gpointer data) {
  activate_url_inner(link);
}


void show_manual_section (const char *lang, const char *section) {
  char *tmp=NULL,*tmp2=NULL;
  const char *link;

  link=g_strdup_printf("%s%s%s%s",LIVES_MANUAL_URL,(lang==NULL?"":(tmp2=g_strdup_printf("//%s//",lang))),
		       LIVES_MANUAL_FILENAME,(section==NULL?"":(tmp=g_strdup_printf("#%s",section))));

  activate_url_inner(link);

  if (tmp!=NULL) g_free(tmp);
  if (tmp2!=NULL) g_free(tmp2);

}


uint64_t
get_file_size(int fd) {
  // get the size of file fd
  struct stat filestat;
  fstat(fd,&filestat);
  return (uint64_t)(filestat.st_size);
}

uint64_t
sget_file_size(const char *name) {
  // get the size of file fd
  struct stat filestat;
  int fd;

  if ((fd=open(name,O_RDONLY))==-1) {
    return (uint32_t)0;
  }

  fstat(fd,&filestat);
  close(fd);

  return (uint64_t)(filestat.st_size);
}


void reget_afilesize (int fileno) {
  // re-get the audio file size
  char *afile;
  file *sfile=mainw->files[fileno];
  boolean bad_header=FALSE;

  if (mainw->multitrack!=NULL) return; // otherwise achans gets set to 0...

  if (!sfile->opening) afile=g_build_filename (prefs->tmpdir,sfile->handle,"audio",NULL);
  else afile=g_build_filename (prefs->tmpdir,sfile->handle,"audiodump.pcm",NULL);
  if ((sfile->afilesize=sget_file_size (afile))==0l) {
    if (!sfile->opening&&fileno!=mainw->ascrap_file&&fileno!=mainw->scrap_file) {
      if (sfile->arate!=0||sfile->achans!=0||sfile->asampsize!=0||sfile->arps!=0) {
	sfile->arate=sfile->achans=sfile->asampsize=sfile->arps=0;
	save_clip_value(fileno,CLIP_DETAILS_ACHANS,&sfile->achans);
	if (mainw->com_failed||mainw->write_failed) bad_header=TRUE;
	save_clip_value(fileno,CLIP_DETAILS_ARATE,&sfile->arps);
	if (mainw->com_failed||mainw->write_failed) bad_header=TRUE;
	save_clip_value(fileno,CLIP_DETAILS_PB_ARATE,&sfile->arate);
	if (mainw->com_failed||mainw->write_failed) bad_header=TRUE;
	save_clip_value(fileno,CLIP_DETAILS_ASAMPS,&sfile->asampsize);
	if (mainw->com_failed||mainw->write_failed) bad_header=TRUE;
	if (bad_header) do_header_write_error(fileno);
      }
    }
  }
  g_free (afile);
}






boolean
create_event_space(int length) {
  // try to create desired events
  // if we run out of memory, all events requested are freed, and we return FALSE
  // otherwise we return TRUE

  // NOTE: this is the OLD event system, it's only used for reordering in the clip editor

  if (cfile->events[0]!=NULL) {
    g_free(cfile->events[0]);
  }
  if ((cfile->events[0]=(event *)(g_try_malloc(sizeof(event)*length)))==NULL) {
    // memory overflow
    return FALSE;
  }
  memset(cfile->events[0],0,length*sizeof(event));
  return TRUE;
}



int lives_list_index (GList *list, const char *data) {
  // find data in list, GTK's version is broken
  // well, actually not broken - but we need to use strcmp

  int i;
  int len;
  if (list==NULL) return -1;

  len=g_list_length (list);

  for (i=0;i<len;i++) {
    if (!strcmp ((char *)g_list_nth_data (list,i),data)) return i;
  }
  return -1;
}






void 
add_to_recent(const char *filename, double start, int frames, const char *extra_params) {
  char buff[PATH_MAX];
  char *file,*tmp;

  if (frames>0) {
    if (extra_params==NULL||(strlen(extra_params)==0)) file=g_strdup_printf ("%s|%.2f|%d",filename,start,frames);
    else file=g_strdup_printf ("%s|%.2f|%d\n%s",filename,start,frames,extra_params);
  }
  else {
    if (extra_params==NULL||(strlen(extra_params)==0)) file=g_strdup (filename);
    else file=g_strdup_printf ("%s\n%s",filename,extra_params);
  }

  get_menu_text(mainw->recent1,buff);
  if (strcmp(file,buff)) {
    get_menu_text(mainw->recent2,buff);
    if (strcmp(file,buff)) {
      get_menu_text(mainw->recent3,buff);
      if (strcmp(file,buff)) {
	// not in list, or at pos 4
	get_menu_text(mainw->recent3,buff);
	set_menu_text(mainw->recent4,buff,FALSE);
	if (mainw->multitrack!=NULL) set_menu_text(mainw->multitrack->recent4,buff,FALSE);
	set_pref("recent4",(tmp=g_filename_from_utf8(buff,-1,NULL,NULL,NULL)));
	g_free(tmp);

	get_menu_text(mainw->recent2,buff);
	set_menu_text(mainw->recent3,buff,FALSE);
	if (mainw->multitrack!=NULL) set_menu_text(mainw->multitrack->recent3,buff,FALSE);
	set_pref("recent3",(tmp=g_filename_from_utf8(buff,-1,NULL,NULL,NULL)));
	g_free(tmp);

	get_menu_text(mainw->recent1,buff);
	set_menu_text(mainw->recent2,buff,FALSE);
	if (mainw->multitrack!=NULL) set_menu_text(mainw->multitrack->recent2,buff,FALSE);
	set_pref("recent2",(tmp=g_filename_from_utf8(buff,-1,NULL,NULL,NULL)));
	g_free(tmp);
	
	set_menu_text(mainw->recent1,file,FALSE);
	if (mainw->multitrack!=NULL) set_menu_text(mainw->multitrack->recent1,file,FALSE);
	set_pref("recent1",(tmp=g_filename_from_utf8(file,-1,NULL,NULL,NULL)));
	g_free(tmp);
      }
      else {
	// #3 in list
	get_menu_text(mainw->recent2,buff);
	set_menu_text(mainw->recent3,buff,FALSE);
	if (mainw->multitrack!=NULL) set_menu_text(mainw->multitrack->recent3,buff,FALSE);
	set_pref("recent3",(tmp=g_filename_from_utf8(buff,-1,NULL,NULL,NULL)));
	g_free(tmp);
	
	get_menu_text(mainw->recent1,buff);
	set_menu_text(mainw->recent2,buff,FALSE);
	if (mainw->multitrack!=NULL) set_menu_text(mainw->multitrack->recent2,buff,FALSE);
	set_pref("recent2",(tmp=g_filename_from_utf8(buff,-1,NULL,NULL,NULL)));
	g_free(tmp);

	set_menu_text(mainw->recent1,file,FALSE);
	if (mainw->multitrack!=NULL) set_menu_text(mainw->multitrack->recent1,file,FALSE);
	set_pref("recent1",(tmp=g_filename_from_utf8(file,-1,NULL,NULL,NULL)));
	g_free(tmp);
      }
    }
    else {
      // #2 in list
      get_menu_text(mainw->recent1,buff);
      set_menu_text(mainw->recent2,buff,FALSE);
      if (mainw->multitrack!=NULL) set_menu_text(mainw->multitrack->recent2,buff,FALSE);
      set_pref("recent2",(tmp=g_filename_from_utf8(buff,-1,NULL,NULL,NULL)));
      g_free(tmp);
	
      set_menu_text(mainw->recent1,file,FALSE);
      if (mainw->multitrack!=NULL) set_menu_text(mainw->multitrack->recent1,file,FALSE);
      set_pref("recent1",(tmp=g_filename_from_utf8(file,-1,NULL,NULL,NULL)));
      g_free(tmp);
    }
  }
  else {
    // I'm number 1, so why change ;-)
  }

  get_menu_text(mainw->recent1,buff);
  if (strlen(buff)) {
    lives_widget_show (mainw->recent1);
  }
  get_menu_text(mainw->recent2,buff);
  if (strlen(buff)) {
    lives_widget_show (mainw->recent2);
  }
  get_menu_text(mainw->recent3,buff);
  if (strlen(buff)) {
    lives_widget_show (mainw->recent3);
  }
  get_menu_text(mainw->recent4,buff);
  if (strlen(buff)) {
    lives_widget_show (mainw->recent4);
  }

  g_free(file);
}




int 
verhash (char *version) {
  char *s;
  int major=0;
  int minor=0;
  int micro=0;

  if (!(strlen(version))) return 0;

  s=strtok (version,".");
  if (!(s==NULL)) {
    major=atoi (s);
    s=strtok (NULL,".");
    if (!(s==NULL)) {
      minor=atoi (s);
      s=strtok (NULL,".");
      if (!(s==NULL)) {
	micro=atoi (s);
      }
    }
  }
  return major*1000000+minor*1000+micro;
}



#ifdef PRODUCE_LOG
// disabled by default
void lives_log(const char *what) {
  char *lives_log_file=g_build_filename(prefs->tmpdir,LIVES_LOG_FILE,NULL);
  if (mainw->log_fd<0) mainw->log_fd=open(lives_log_file,O_WRONLY|O_CREAT,DEF_FILE_PERMS);
  if (mainw->log_fd!=-1) {
    char *msg=g_strdup("%s|%d|",what,mainw->current_file);
    write (mainw->log_fd,msg,strlen(msg));
    g_free(msg);
  }
  g_free(lives_log_file);
}
#endif




// TODO - move into undo.c
void 
set_undoable (const char *what, boolean sensitive) {
  if (mainw->current_file>-1) {
    cfile->redoable=FALSE;
    cfile->undoable=sensitive;
    if (!(what==NULL)) {
      char *what_safe=g_strdelimit (g_strdup (what),"_",' ');
      g_snprintf(cfile->undo_text,32,_ ("_Undo %s"),what_safe);
      g_snprintf(cfile->redo_text,32,_ ("_Redo %s"),what_safe);
      g_free (what_safe);
    }
    else {
      cfile->undoable=FALSE;
      cfile->undo_action=UNDO_NONE;
      g_snprintf(cfile->undo_text,32,"%s",_ ("_Undo"));
      g_snprintf(cfile->redo_text,32,"%s",_ ("_Redo"));
    }
    set_menu_text(mainw->undo,cfile->undo_text,TRUE);
    set_menu_text(mainw->redo,cfile->redo_text,TRUE);
  }

  lives_widget_hide(mainw->redo);
  lives_widget_show(mainw->undo);
  lives_widget_set_sensitive (mainw->undo,sensitive);

#ifdef PRODUCE_LOG
  lives_log(what);
#endif


}

void 
set_redoable (const char *what, boolean sensitive) {
  if (mainw->current_file>-1) {
    cfile->undoable=FALSE;
    cfile->redoable=sensitive;
    if (!(what==NULL)) {
      char *what_safe=g_strdelimit (g_strdup (what),"_",' ');
      g_snprintf(cfile->undo_text,32,_ ("_Undo %s"),what_safe);
      g_snprintf(cfile->redo_text,32,_ ("_Redo %s"),what_safe);
      g_free (what_safe);
    }
    else {
      cfile->redoable=FALSE;
      cfile->undo_action=UNDO_NONE;
      g_snprintf(cfile->undo_text,32,"%s",_ ("_Undo"));
      g_snprintf(cfile->redo_text,32,"%s",_ ("_Redo"));
    }
    set_menu_text(mainw->undo,cfile->undo_text,TRUE);
    set_menu_text(mainw->redo,cfile->redo_text,TRUE);
  }

  lives_widget_hide(mainw->undo);
  lives_widget_show(mainw->redo);
  lives_widget_set_sensitive (mainw->redo,sensitive);
}


void 
set_sel_label (GtkWidget *sel_label) {
  char *tstr,*frstr,*tmp;
  char *sy,*sz;

  if (mainw->current_file==-1||!cfile->frames||mainw->multitrack!=NULL) {
    lives_label_set_text(LIVES_LABEL(sel_label),_ ("-------------Selection------------"));
  }
  else {
    tstr=g_strdup_printf ("%.2f",calc_time_from_frame (mainw->current_file,cfile->end+1)-
			  calc_time_from_frame (mainw->current_file,cfile->start));
    frstr=g_strdup_printf ("%d",cfile->end-cfile->start+1);

    // TRANSLATORS: - try to keep the text of the middle part the same length, by deleting "-" if necessary
    lives_label_set_text(LIVES_LABEL(sel_label),(tmp=g_strconcat ("---------- [ ",tstr,(sy=(g_strdup(_(" sec ] ----------Selection---------- [ ")))),frstr,(sz=g_strdup(_(" frames ] ----------"))),NULL)));
    g_free(sy);
    g_free(sz);

    g_free (tmp);
    g_free (frstr);
    g_free (tstr);
  }
  lives_widget_queue_draw (sel_label);
}




LIVES_INLINE void g_list_free_strings(GList *slist) {
  GList *list=slist;
  if (list==NULL) return;
  while (list!=NULL) {
    if (list->data!=NULL) {
      //g_printerr("free %s\n",list->data);
      g_free(list->data);
    }
    list=list->next;
  }
}


boolean cache_file_contents(const char *filename) {
  FILE *hfile;
  char buff[65536];

  if (mainw->cached_list!=NULL) {
    g_list_free_strings(mainw->cached_list);
    g_list_free(mainw->cached_list);
    mainw->cached_list=NULL;
  }

  if (!(hfile=fopen(filename,"r"))) return FALSE;
  while (fgets(buff,65536,hfile)!=NULL) {
    mainw->cached_list=g_list_append(mainw->cached_list,g_strdup(buff));
    threaded_dialog_spin();
  }
  fclose(hfile);
  return TRUE;
}


char *get_val_from_cached_list(const char *key, size_t maxlen) {
  GList *clist=mainw->cached_list;
  char *keystr_start=g_strdup_printf("<%s>",key);
  char *keystr_end=g_strdup_printf("</%s>",key);
  size_t kslen=strlen(keystr_start);
  size_t kelen=strlen(keystr_end);

  boolean gotit=FALSE;
  char buff[maxlen];

  memset(buff,0,1);

  while (clist!=NULL) {
    if (gotit) {
      if (!strncmp(keystr_end,(char *)clist->data,kelen)) {
	break;
      }
      if (strncmp((char *)clist->data,"|",1)) g_strappend(buff,maxlen,(char *)clist->data);
      else {
	if (clist->prev!=NULL) clist->prev->next=clist->next;
      }
    }
    else if (!strncmp(keystr_start,(char *)clist->data,kslen)) {
      gotit=TRUE;
    }
    clist=clist->next;
  }
  g_free(keystr_start);
  g_free(keystr_end);

  if (!gotit) return NULL;

  if (strlen(buff)>0) memset(buff+strlen(buff)-1,0,1); // remove trailing newline

  return g_strdup(buff);
}




char *clip_detail_to_string(lives_clip_details_t what, size_t *maxlenp) {
  char *key=NULL;

  switch (what) {
  case CLIP_DETAILS_HEADER_VERSION:
    key=g_strdup("header_version");
    if (maxlenp!=NULL) *maxlenp=256;
    break;
  case CLIP_DETAILS_BPP:
    key=g_strdup("bpp");
    if (maxlenp!=NULL) *maxlenp=256;
    break;
  case CLIP_DETAILS_FPS:
    key=g_strdup("fps");
    if (maxlenp!=NULL) *maxlenp=256;
    break;
  case CLIP_DETAILS_PB_FPS:
    key=g_strdup("pb_fps");
    if (maxlenp!=NULL) *maxlenp=256;
    break;
  case CLIP_DETAILS_WIDTH:
    key=g_strdup("width");
    if (maxlenp!=NULL) *maxlenp=256;
    break;
  case CLIP_DETAILS_HEIGHT:
    key=g_strdup("height");
    if (maxlenp!=NULL) *maxlenp=256;
    break;
  case CLIP_DETAILS_UNIQUE_ID:
    key=g_strdup("unique_id");
    if (maxlenp!=NULL) *maxlenp=256;
    break;
  case CLIP_DETAILS_ARATE:
    key=g_strdup("audio_rate");
    if (maxlenp!=NULL) *maxlenp=256;
    break;
  case CLIP_DETAILS_PB_ARATE:
    key=g_strdup("pb_audio_rate");
    if (maxlenp!=NULL) *maxlenp=256;
    break;
  case CLIP_DETAILS_ACHANS:
    key=g_strdup("audio_channels");
    if (maxlenp!=NULL) *maxlenp=256;
    break;
  case CLIP_DETAILS_ASIGNED:
    key=g_strdup("audio_signed");
    if (maxlenp!=NULL) *maxlenp=256;
    break;
  case CLIP_DETAILS_AENDIAN:
    key=g_strdup("audio_endian");
    if (maxlenp!=NULL) *maxlenp=256;
    break;
  case CLIP_DETAILS_ASAMPS:
    key=g_strdup("audio_sample_size");
    if (maxlenp!=NULL) *maxlenp=256;
    break;
  case CLIP_DETAILS_FRAMES:
    key=g_strdup("frames");
    if (maxlenp!=NULL) *maxlenp=256;
    break;
  case CLIP_DETAILS_TITLE:
    key=g_strdup("title");
    break;
  case CLIP_DETAILS_AUTHOR:
    key=g_strdup("author");
    break;
  case CLIP_DETAILS_COMMENT:
    key=g_strdup("comment");
    break;
  case CLIP_DETAILS_KEYWORDS:
    key=g_strdup("keywords");
    break;
  case CLIP_DETAILS_PB_FRAMENO:
    key=g_strdup("pb_frameno");
    if (maxlenp!=NULL) *maxlenp=256;
    break;
  case CLIP_DETAILS_CLIPNAME:
    key=g_strdup("clipname");
    break;
  case CLIP_DETAILS_FILENAME:
    key=g_strdup("filename");
    break;
  case CLIP_DETAILS_INTERLACE:
    key=g_strdup("interlace");
    break;
  default:
    break;
  }
  return key;
}



boolean get_clip_value(int which, lives_clip_details_t what, void *retval, size_t maxlen) {
  FILE *valfile;
  time_t old_time=0,new_time=0;
  struct stat mystat;

  char *vfile;
  char *lives_header=NULL;
  char *old_header;
  char *com;
  char *val;
  char *key;
  char *tmp;

  int alarm_handle;
  int retval2=0;

  boolean timeout;

  if (mainw->cached_list==NULL) {
    
    lives_header=g_build_filename(prefs->tmpdir,mainw->files[which]->handle,"header.lives",NULL);
    old_header=g_build_filename(prefs->tmpdir,mainw->files[which]->handle,"header",NULL);
    
    // TODO - remove this some time before 2038
    if (!stat(old_header,&mystat)) old_time=mystat.st_mtime;
    if (!stat(lives_header,&mystat)) new_time=mystat.st_mtime;
    g_free(old_header);
    
    if (old_time>new_time) {
      g_free(lives_header);
      return FALSE; // clip has been edited by an older version of LiVES
    }
  }
  //////////////////////////////////////////////////
  key=clip_detail_to_string(what,&maxlen);

  if (key==NULL) {
    tmp=g_strdup_printf("Invalid detail %d requested from file %s",which,lives_header);
    LIVES_ERROR(tmp);
    g_free(tmp);
    g_free(lives_header);
    return FALSE;
  }

  mainw->read_failed=FALSE;

  if (mainw->cached_list!=NULL) {
    val=get_val_from_cached_list(key,maxlen);
    g_free(key);
    if (val==NULL) return FALSE;
  }
  else {
    com=g_strdup_printf("%s get_clip_value \"%s\" %d %d \"%s\"",prefs->backend_sync,key,
			lives_getuid(),lives_getpid(),lives_header);
    g_free(lives_header);
    g_free(key);
    
    val=(char *)g_malloc(maxlen);
    memset(val,0,maxlen);
    
    threaded_dialog_spin();

    if (lives_system(com,TRUE)) {
      tempdir_warning();
      threaded_dialog_spin();
      g_free(com);
      return FALSE;
    }
    
#ifndef IS_MINGW
    vfile=g_strdup_printf("%s/.smogval.%d.%d",prefs->tmpdir,lives_getuid(),lives_getpid());
#else
    vfile=g_strdup_printf("%s/smogval.%d.%d",prefs->tmpdir,lives_getuid(),lives_getpid());
#endif

    do {
      retval2=0;
      timeout=FALSE;
      mainw->read_failed=FALSE;
      
      alarm_handle=lives_alarm_set(LIVES_PREFS_TIMEOUT);
      
      do {
	if (!((valfile=fopen(vfile,"r")) || (timeout=lives_alarm_get(alarm_handle)))) {
	  if (!timeout) {
	    if (!(mainw==NULL)) {
	      weed_plant_t *frame_layer=mainw->frame_layer;
	      mainw->frame_layer=NULL;
	      lives_widget_context_update();
	      mainw->frame_layer=frame_layer;
	    }
	    g_usleep(prefs->sleep_time);
	  }
	  else break;
	}
      } while (!valfile);
      
      lives_alarm_clear(alarm_handle);
      
      if (timeout) {
	retval2=do_read_failed_error_s_with_retry(vfile,NULL,NULL);
      }
      else {
	mainw->read_failed=FALSE;
	lives_fgets(val,maxlen,valfile);
	fclose(valfile);
	unlink(vfile);
	if (mainw->read_failed) {
	  retval2=do_read_failed_error_s_with_retry(vfile,NULL,NULL);
	}
      }
    } while (retval2==LIVES_RETRY);
    
    g_free(vfile);
    g_free(com);
  }

  if (retval2==LIVES_CANCEL) {
    return FALSE;
  }

  switch (what) {
  case CLIP_DETAILS_BPP:
  case CLIP_DETAILS_WIDTH:
  case CLIP_DETAILS_HEIGHT:
  case CLIP_DETAILS_ARATE:
  case CLIP_DETAILS_ACHANS:
  case CLIP_DETAILS_ASAMPS:
  case CLIP_DETAILS_FRAMES:
  case CLIP_DETAILS_HEADER_VERSION:
    *(int *)retval=atoi(val);
    break;
  case CLIP_DETAILS_ASIGNED:
    *(int *)retval=0;
    if (mainw->files[which]->header_version==0) *(int *)retval=atoi(val);
    if (*(int *)retval==0&&(!strcasecmp(val,"false"))) *(int *)retval=1; // unsigned
    break;
  case CLIP_DETAILS_PB_FRAMENO:
    *(int *)retval=atoi(val);
    if (retval==0) *(int *)retval=1;
    break;
  case CLIP_DETAILS_PB_ARATE:
    *(int *)retval=atoi(val);
    if (retval==0) *(int *)retval=mainw->files[which]->arps;
    break;
  case CLIP_DETAILS_INTERLACE:
    *(int *)retval=atoi(val);
    break;
  case CLIP_DETAILS_FPS:
    *(double *)retval=strtod(val,NULL);
    if (*(double *)retval==0.) *(double *)retval=prefs->default_fps;
    break;
  case CLIP_DETAILS_PB_FPS:
    *(double *)retval=strtod(val,NULL);
    if (*(double *)retval==0.) *(double *)retval=mainw->files[which]->fps;
    break;
  case CLIP_DETAILS_UNIQUE_ID:
    if (capable->cpu_bits==32) {
      *(int64_t *)retval=strtoll(val,NULL,10);
    }
    else {
      *(int64_t *)retval=strtol(val,NULL,10);
    }
    break;
  case CLIP_DETAILS_AENDIAN:
    *(int *)retval=atoi(val)*2;
    break;
  case CLIP_DETAILS_TITLE:
  case CLIP_DETAILS_AUTHOR:
  case CLIP_DETAILS_COMMENT:
  case CLIP_DETAILS_CLIPNAME:
  case CLIP_DETAILS_KEYWORDS:
    g_snprintf((char *)retval,maxlen,"%s",val);
    break;
  case CLIP_DETAILS_FILENAME:
    g_snprintf((char *)retval,maxlen,"%s",(tmp=g_filename_to_utf8(val,-1,NULL,NULL,NULL)));
    g_free(tmp);
    break;
  }
  g_free(val);
  return TRUE;
}



void save_clip_value(int which, lives_clip_details_t what, void *val) {
  char *lives_header;
  char *com,*tmp;
  char *myval;
  char *key;

  boolean needs_sigs=FALSE;

  mainw->write_failed=mainw->com_failed=FALSE;

  if (which==0||which==mainw->scrap_file) return;

  lives_header=g_build_filename(prefs->tmpdir,mainw->files[which]->handle,"header.lives",NULL);
  key=clip_detail_to_string(what,NULL);

  if (key==NULL) {
    tmp=g_strdup_printf("Invalid detail %d added for file %s",which,lives_header);
    LIVES_ERROR(tmp);
    g_free(tmp);
    g_free(lives_header);
    return;
  }

  switch (what) {
  case CLIP_DETAILS_BPP:
    myval=g_strdup_printf("%d",*(int *)val);
    break;
  case CLIP_DETAILS_FPS:
    if (!mainw->files[which]->ratio_fps) myval=g_strdup_printf("%.3f",*(double *)val);
    else myval=g_strdup_printf("%.8f",*(double *)val);
    break;
  case CLIP_DETAILS_PB_FPS:
    if (mainw->files[which]->ratio_fps&&(mainw->files[which]->pb_fps==mainw->files[which]->fps)) 
      myval=g_strdup_printf("%.8f",*(double *)val);
    else myval=g_strdup_printf("%.3f",*(double *)val);
    break;
  case CLIP_DETAILS_WIDTH:
    myval=g_strdup_printf("%d",*(int *)val);
    break;
  case CLIP_DETAILS_HEIGHT:
    myval=g_strdup_printf("%d",*(int *)val);
    break;
  case CLIP_DETAILS_UNIQUE_ID:
    myval=g_strdup_printf("%"PRId64,*(int64_t *)val);
    break;
  case CLIP_DETAILS_ARATE:
    myval=g_strdup_printf("%d",*(int *)val);
    break;
  case CLIP_DETAILS_PB_ARATE:
    myval=g_strdup_printf("%d",*(int *)val);
    break;
  case CLIP_DETAILS_ACHANS:
    myval=g_strdup_printf("%d",*(int *)val);
    break;
  case CLIP_DETAILS_ASIGNED:
    if (*(int *)val==1) myval=g_strdup("true");
    else myval=g_strdup("false");
    break;
  case CLIP_DETAILS_AENDIAN:
    myval=g_strdup_printf("%d",*(int *)val/2);
    break;
  case CLIP_DETAILS_ASAMPS:
    myval=g_strdup_printf("%d",*(int *)val);
    break;
  case CLIP_DETAILS_FRAMES:
    myval=g_strdup_printf("%d",*(int *)val);
    break;
  case CLIP_DETAILS_INTERLACE:
    myval=g_strdup_printf("%d",*(int *)val);
    break;
  case CLIP_DETAILS_TITLE:
    myval=g_strdup((char *)val);
    break;
  case CLIP_DETAILS_AUTHOR:
    myval=g_strdup((char *)val);
    break;
  case CLIP_DETAILS_COMMENT:
    myval=g_strdup((char *)val);
    break;
  case CLIP_DETAILS_KEYWORDS:
    myval=g_strdup((char *)val);
    break;
  case CLIP_DETAILS_PB_FRAMENO:
    myval=g_strdup_printf("%d",*(int *)val);
    break;
  case CLIP_DETAILS_CLIPNAME:
    myval=g_strdup((char *)val);
    break;
  case CLIP_DETAILS_FILENAME:
    myval=g_filename_from_utf8((char *)val,-1,NULL,NULL,NULL);
    break;
  case CLIP_DETAILS_HEADER_VERSION:
    myval=g_strdup_printf("%d",*(int *)val);
    break;
  default:
    return;
  }

  if (mainw->clip_header!=NULL) {
    char *keystr_start=g_strdup_printf("<%s>\n",key);
    char *keystr_end=g_strdup_printf("\n</%s>\n",key);
    lives_fputs(keystr_start,mainw->clip_header);
    lives_fputs(myval,mainw->clip_header);
    lives_fputs(keystr_end,mainw->clip_header);
    g_free(keystr_start);
    g_free(keystr_end);

  }
  else {
    if (!mainw->signals_deferred) {
      set_signal_handlers((SignalHandlerPointer)defer_sigint);
      needs_sigs=TRUE;
    }
    com=g_strdup_printf("%s set_clip_value \"%s\" \"%s\" \"%s\"",prefs->backend_sync,lives_header,key,myval);
    lives_system(com,FALSE);
    if (mainw->signal_caught) catch_sigint(mainw->signal_caught);
    if (needs_sigs) set_signal_handlers((SignalHandlerPointer)catch_sigint);
    g_free(com);
  }

  g_free(lives_header);
  g_free(myval);
  g_free(key);
  
  return;
}



GList *get_set_list(const char *dir) {
  // get list of sets in top level dir
  GList *setlist=NULL;
  DIR *tldir,*subdir;
  struct dirent *tdirent,*subdirent;
  char *subdirname;

  if (dir==NULL) return NULL;

  tldir=opendir(dir);

  if (tldir==NULL) return NULL;

  lives_set_cursor_style(LIVES_CURSOR_BUSY,NULL);
  lives_widget_context_update();


  while (1) {
    tdirent=readdir(tldir);

    if (tdirent==NULL) {
      closedir(tldir);
      lives_set_cursor_style(LIVES_CURSOR_NORMAL,NULL);
      return setlist;
    }
    
    if (!strncmp(tdirent->d_name,"..",strlen(tdirent->d_name))) continue;

    subdirname=g_build_filename(dir,tdirent->d_name,NULL);

    subdir=opendir(subdirname);

    if (subdir==NULL) {
      g_free(subdirname);
      continue;
    }

    while (1) {
      subdirent=readdir(subdir);

      if (subdirent==NULL) {
	break;
      }

      if (!strcmp(subdirent->d_name,"order")) {
	setlist=g_list_append(setlist,g_strdup(tdirent->d_name));
	break;
      }
    }
    g_free(subdirname);
    closedir(subdir);
  }

}




boolean check_for_ratio_fps (double fps) {
  boolean ratio_fps;
  char *test_fps_string1=g_strdup_printf ("%.3f00000",fps);
  char *test_fps_string2=g_strdup_printf ("%.8f",fps);
  
  if (strcmp (test_fps_string1,test_fps_string2)) {
    // got a ratio
    ratio_fps=TRUE;
  }
  else {
    ratio_fps=FALSE;
  }
  g_free (test_fps_string1);
  g_free (test_fps_string2);

  return ratio_fps;
}


double get_ratio_fps(const char *string) {
  // return a ratio (8dp) fps from a string with format num:denom
  double fps;
  char *fps_string;
  char **array=g_strsplit(string,":",2);
  int num=atoi (array[0]);
  int denom=atoi (array[1]);
  g_strfreev (array);
  fps=(double)num/(double)denom;
  fps_string=g_strdup_printf("%.8f",fps);
  fps=g_strtod(fps_string,NULL);
  g_free(fps_string);
  return fps;
}



char *remove_trailing_zeroes(double val) {
  int i;
  double xval=val;

  if (val==(int)val) return g_strdup_printf("%d",(int)val);
  for (i=0;i<=16;i++) {
    xval*=10.;
    if (xval==(int)xval) return g_strdup_printf("%.*f",i,val);
  }
  return g_strdup_printf("%.*f",i,val);
}


uint32_t get_signed_endian (boolean is_signed, boolean little_endian) {
  // asigned TRUE == signed, FALSE == unsigned


  if (is_signed) {
    if (little_endian) {
      return 0;
    }
    else {
      return AFORM_BIG_ENDIAN;
    }
  }
  else {
    if (!is_signed) { 
      if (little_endian) {
	return AFORM_UNSIGNED;
      }
      else {
	return AFORM_UNSIGNED|AFORM_BIG_ENDIAN;
      }
    }
  }
  return AFORM_UNKNOWN;
}




int get_token_count (const char *string, int delim) {
  int pieces=1;
  if (string==NULL) return 0;
  if (delim<=0||delim>255) return 1;

  while ((string=strchr(string,delim))!=NULL) {
    pieces++;
    string++;
  }
  return pieces;
}



char *subst (const char *string, const char *from, const char *to) {
  // return a string with all occurrences of from replaced with to
  // return value should be freed after use
  char *ret=g_strdup(string),*first;
  char *search=ret;

  while ((search=g_strstr_len (search,-1,from))!=NULL) {
    first=g_strndup(ret,search-ret);
    search=g_strdup(search+strlen(from));
    g_free(ret);
    ret=g_strconcat (first,to,search,NULL);
    g_free(search);
    search=ret+strlen(first)+strlen(to);
    g_free(first);
  }
  return ret;
}

char *insert_newlines(const char *text, int maxwidth) {
  // crude formating of strings, ensure a newline after every run of maxwidth chars
  // does not take into account for example utf8 multi byte chars

  char newline[]="\n";
  char *retstr;
  register int i;
  int xtoffs;
  boolean needsnl=FALSE;
  size_t req_size=1;  // for the terminating \0
  size_t tlen;
  size_t nlen=strlen(newline);
  size_t runlen=0;
  wchar_t utfsym;

  if (text==NULL) return NULL;

  if (maxwidth<1) return g_strdup("Bad maxwidth, dummy");

  tlen=strlen(text);

  xtoffs=mbtowc(NULL,NULL,0); // reset read state


  //pass 1, get the required size
  for (i=0;i<tlen;i+=xtoffs) {
    xtoffs=mbtowc(&utfsym,&text[i],4); // get next utf8 wchar
    if (xtoffs==-1) {
      LIVES_WARN("mbtowc returned -1");
      return g_strdup(text);
    }

    if (!strncmp(text+i,"\n",nlen)) runlen=0; // is a newline (in any encoding)
    else {
      runlen++;
      if (needsnl) req_size+=nlen; ///< we will insert a nl here
    }

    if (runlen==maxwidth) {
      if (i<tlen-1 && (strncmp(text+i+1,"\n",nlen))) {
	// needs a newline
	needsnl=TRUE;
	runlen=0;
      }
    }
    else needsnl=FALSE;
    req_size+=xtoffs;
  }


  retstr=(char *)g_malloc(req_size);
  req_size=0; // reuse as a ptr to offset in retstr
  runlen=0;
  needsnl=FALSE;

  //pass 2, copy and insert newlines

  for (i=0;i<tlen;i+=xtoffs) {

    xtoffs=mbtowc(&utfsym,&text[i],4); // get next utf8 wchar
    if (!strncmp(text+i,"\n",nlen)) runlen=0; // is a newline (in any encoding)
    else {
      runlen++;
      if (needsnl) {
	memcpy(retstr+req_size,newline,nlen);
	req_size+=nlen;
      }
    }

    if (runlen==maxwidth) {
      if (i<tlen-1 && (strncmp(text+i+1,"\n",nlen))) {
	// needs a newline
	needsnl=TRUE;
	runlen=0;
      }
    }
    else needsnl=FALSE;
    memcpy(retstr+req_size,&utfsym,xtoffs);
    req_size+=xtoffs;

  }

  memset(retstr+req_size,0,1);
  
  return retstr;
}



int hextodec (const char *string) {
  int i;
  int tot=0;
  char test[2];

  memset (test+1,0,1);

  for (i=0;i<strlen (string);i++) {
    tot*=16;
    lives_memcpy (test,&string[i],1);
    tot+=get_hex_digit (test);
  }
  return tot;
}

int get_hex_digit (const char *c) {
  if (!strcmp (c,"a")||!strcmp (c,"A")) return 10;
  if (!strcmp (c,"b")||!strcmp (c,"B")) return 11;
  if (!strcmp (c,"c")||!strcmp (c,"C")) return 12;
  if (!strcmp (c,"d")||!strcmp (c,"D")) return 13;
  if (!strcmp (c,"e")||!strcmp (c,"E")) return 14;
  if (!strcmp (c,"f")||!strcmp (c,"F")) return 15;
  return(atoi (c));
}



static uint32_t fastrand_val;

LIVES_INLINE uint32_t fastrand(void)
{
#define rand_a 1073741789L
#define rand_c 32749L

  return (fastrand_val= rand_a * fastrand_val + rand_c);
}

void fastsrand(uint32_t seed)
{
  fastrand_val = seed;
}


boolean is_writeable_dir(const char *dir) {
  // return 0 if we cannot create/write to dir

  // dir should be in locale encoding
#ifndef IS_MINGW
  struct statvfs sbuf;
#else
  char *com;
#endif

  if (!g_file_test(dir,G_FILE_TEST_IS_DIR)) {
    g_mkdir_with_parents(dir,S_IRWXU);
    if (!g_file_test(dir,G_FILE_TEST_IS_DIR)) {
      return FALSE;
    }
  }

#ifndef IS_MINGW
  // use statvfs to get fs details
  if (statvfs(dir,&sbuf)==-1) return FALSE;
  if (sbuf.f_flag&ST_RDONLY) return FALSE;
#else
  mainw->com_failed=FALSE;
  com=g_strdup_printf("touch.exe \"%s\\xxxxfile.txt\"",dir);
  lives_system(com,TRUE);
  g_free(com);
  com=g_strdup_printf("%s\\xxxxfile.txt",dir);
  unlink(com);
  g_free(com);
  if (mainw->com_failed) return FALSE;
#endif
  return TRUE;
}




uint64_t get_fs_free(const char *dir) {
  // get free space in bytes for volume containing directory dir
  // return 0 if we cannot create/write to dir

  // caller should test with is_writeable_dir() first before calling this
  // since 0 is a valid return value

  // dir should be in locale encoding

#ifndef IS_MINGW
  struct statvfs sbuf;
#endif

  uint64_t bytes=0;
  boolean must_delete=FALSE;

  if (!g_file_test(dir,G_FILE_TEST_IS_DIR)) must_delete=TRUE;
  if (!is_writeable_dir(dir)) goto getfserr;

#ifndef IS_MINGW

  // use statvfs to get fs details
  if (statvfs(dir,&sbuf)==-1) goto getfserr;
  if (sbuf.f_flag&ST_RDONLY) goto getfserr;

  // result is block size * blocks available
  bytes=sbuf.f_bsize*sbuf.f_bavail;

#else
  GetDiskFreeSpaceEx(dir,(PULARGE_INTEGER)&bytes,NULL,NULL);
#endif

getfserr:
  if (must_delete) rmdir(dir);

  return bytes;
}




LIVES_INLINE LiVESInterpType get_interp_value(gshort quality) {
  if (quality==PB_QUALITY_HIGH) return LIVES_INTERP_BEST;
  else if (quality==PB_QUALITY_MED) return LIVES_INTERP_NORMAL;
  return LIVES_INTERP_FAST;
}



LIVES_INLINE GList *g_list_move_to_first(GList *list, GList *item) {
  // move item to first in list
  GList *xlist=g_list_remove_link(list,item); // item becomes standalone list
  return g_list_concat(item,xlist); // concat rest of list after item
}


GList *g_list_delete_string(GList *list, char *string) {
  // remove string from list, using strcmp

  GList *xlist=list;
  while (xlist!=NULL) {
    if (!strcmp((char *)xlist->data,string)) {
      if (xlist->prev!=NULL) xlist->prev->next=xlist->next;
      else list=xlist->next;
      if (xlist->next!=NULL) xlist->next->prev=xlist->prev;
      xlist->next=xlist->prev=NULL;
      g_free(xlist->data);
      g_list_free(xlist);
      return list;
    }
    xlist=xlist->next;
  }
  return list;
}


GList *g_list_copy_strings(GList *list) {
  // copy a list, copying the strings too

  GList *xlist=NULL,*olist=list;

  while (olist!=NULL) {
    xlist=g_list_append(xlist,g_strdup((gchar *)olist->data));
    olist=olist->next;
  }

  return xlist;
}




boolean string_lists_differ(GList *alist, GList *blist) {
  // compare 2 lists of strings and see if they are different (ignoring ordering)
  // for long lists this would be quicker if we sorted the lists first; however this function 
  // is designed to deal with short lists only


  GList *plist;

  if (g_list_length(alist)!=g_list_length(blist)) return TRUE; // check the simple case first

  // run through alist and see if we have a mismatch

  plist=alist;
  while (plist!=NULL) {
    GList *qlist=blist;
    boolean matched=FALSE;
    while (qlist!=NULL) {
      if (!(strcmp((char *)plist->data,(char *)qlist->data))) {
	matched=TRUE;
	break;
      }
      qlist=qlist->next;
    }
    if (!matched) return TRUE;
    plist=plist->next;
  }

  // since both lists were of the same length, there is no need to check blist

  return FALSE;
}



lives_cancel_t check_for_bad_ffmpeg(void) {
  int i;

  int fcount,ofcount;
  
  char *fname_next;

  boolean maybeok=FALSE;

  ofcount=cfile->frames;

  get_frame_count(mainw->current_file);

  fcount=cfile->frames;

  for (i=1;i<=fcount;i++) {
    fname_next=g_strdup_printf("%s/%s/%08d.%s",prefs->tmpdir,cfile->handle,i,prefs->image_ext);
    if (sget_file_size(fname_next)>0) {
      maybeok=TRUE;
      break;
    }
  }

  cfile->frames=ofcount;

  if (!maybeok) {
    do_error_dialog(_("Your version of mplayer/ffmpeg may be broken !\nSee http://bugzilla.mplayerhq.hu/show_bug.cgi?id=2071\n\nYou can work around this temporarily by switching to jpeg output in Preferences/Decoding.\n\nTry running Help/Troubleshoot for more information."));
    return CANCEL_ERROR;
  }
  return CANCEL_NONE;
}

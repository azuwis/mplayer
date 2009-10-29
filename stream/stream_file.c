/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "mp_msg.h"
#include "stream.h"
#include "help_mp.h"
#include "m_option.h"
#include "m_struct.h"

static struct stream_priv_s {
  char* filename;
  char *filename2;
} stream_priv_dflts = {
  NULL, NULL
};

#define ST_OFF(f) M_ST_OFF(struct stream_priv_s,f)
/// URL definition
static const m_option_t stream_opts_fields[] = {
  {"string", ST_OFF(filename), CONF_TYPE_STRING, 0, 0 ,0, NULL},
  {"filename", ST_OFF(filename2), CONF_TYPE_STRING, 0, 0 ,0, NULL},
  { NULL, NULL, 0, 0, 0, 0,  NULL }
};
static const struct m_struct_st stream_opts = {
  "file",
  sizeof(struct stream_priv_s),
  &stream_priv_dflts,
  stream_opts_fields
};

typedef struct {
  char *filename;
  unsigned short headSize; /* headSize == 0 means those are split volumes, not rar volumes */
  unsigned long packSize;
  unsigned short idx;
  unsigned char naming;
  char *basename;
  off_t fileSize;
  unsigned long *splitSize; /* only used in split volumes */
} stream_0day_priv_t;

static int open_0day_volume(stream_0day_priv_t *p, int i)
{
  int h;
  char *name = (char*)malloc(260);

  if (!p->naming) {
    if (i)
      sprintf(name, "%s%02d", p->basename, i - 1);
    else
      sprintf(name, "%sar", p->basename);
  }
  else if(p->headSize == 0) {
    sprintf(name, "%s%0*d", p->basename, p->naming, i + 1);
  }
  else
    sprintf(name, "%s%0*d.rar", p->basename, p->naming, i + 1);

  h = open(name, O_RDONLY);
  if (h >= 0) {
    p->idx = i;
    mp_msg(MSGT_OPEN,MSGL_INFO,"[concat] opening %s\n", name);
  }

  free(name);

  return h;
}

static void close_0day(stream_t *s)
{
  stream_0day_priv_t *p = (stream_0day_priv_t *)s->priv;

  free(p->splitSize);
  free(p->filename);
  free(p->basename);
  free(p);
}

static int seek_0day(stream_t *s,off_t newpos)
{
  stream_0day_priv_t *p = (stream_0day_priv_t *)s->priv;
  int i, fd;

  s->pos = newpos;
  if (newpos < 0 || newpos > p->fileSize) {
    s->eof = 1;
    return 0;
  }

  i = newpos / p->packSize;
  if (i != p->idx) {
    fd = open_0day_volume(p, i);
    if (fd < 0) {
      s->eof = 1;
      return 0;
    }
    close(s->fd);
    s->fd = fd;
  }

  lseek(s->fd, newpos-i*((off_t)p->packSize)+p->headSize, SEEK_SET);
  return 1;
}

static int seek_concat(stream_t *s,off_t newpos)
{
  stream_0day_priv_t *p = (stream_0day_priv_t *)s->priv;
  int i, fd;
  off_t total = 0;

  s->pos = newpos;
  if (newpos < 0 || newpos > p->fileSize) {
    s->eof = 1;
    return 0;
  }

  for(i = 0; total <= newpos; i++) {
    total += p->splitSize[i];
  }
  i--;
  if (i != p->idx) {
    fd = open_0day_volume(p, i);
    if (fd < 0) {
      s->eof = 1;
      return 0;
    }
    close(s->fd);
    s->fd = fd;
  }

  lseek(s->fd, newpos-total+p->splitSize[i], SEEK_SET);
  return 1;
}

static int fill_buffer_0day(stream_t *s, char* buffer, int max_len)
{
  int r, fd;
  int k;
  off_t i, j;
  stream_0day_priv_t *p = (stream_0day_priv_t *)s->priv;

  if ((s->pos >= p->fileSize) && (max_len > 0)) {
    close(s->fd);
    s->fd = -1;
    return -1;
  }

  i = s->pos + max_len;
  if (i > p->fileSize) {
    i = p->fileSize;
    max_len = i - s->pos;
  }
  j = (p->idx+1)*((off_t)p->packSize);
  if (i > j) {
    j = max_len-(i-j);
    r = read(s->fd, buffer, j);
    if (r < j) goto exit_fill_buffer_0day;

    fd = open_0day_volume(p, p->idx+1);
    if (fd < 0) {
      s->eof = 1;
      goto exit_fill_buffer_0day;
    }
    close(s->fd);
    s->fd = fd;
    lseek(s->fd, p->headSize, SEEK_SET);
    r += read(s->fd, buffer + r, max_len - r);
  } else
    r = read(s->fd, buffer, max_len);

exit_fill_buffer_0day:
  return (r <= 0) ? -1 : r;
}

static int fill_buffer_concat(stream_t *s, char* buffer, int max_len)
{
  int r, fd;
  int k;
  off_t i, j;
  stream_0day_priv_t *p = (stream_0day_priv_t *)s->priv;

  if ((s->pos >= p->fileSize) && (max_len > 0)) {
    close(s->fd);
    s->fd = -1;
    return -1;
  }

  i = s->pos + max_len;
  if (i > p->fileSize) {
    i = p->fileSize;
    max_len = i - s->pos;
  }
  j = 0;
  for (k = 0; k <= p->idx; k++) {
    j += p->splitSize[k];
  }
  if (i > j) {
    j = max_len-(i-j);
    r = read(s->fd, buffer, j);
    if (r < j) goto exit_fill_buffer_concat;

    fd = open_0day_volume(p, p->idx+1);
    if (fd < 0) {
      s->eof = 1;
      goto exit_fill_buffer_concat;
    }
    close(s->fd);
    s->fd = fd;
    lseek(s->fd, p->headSize, SEEK_SET);
    r += read(s->fd, buffer + r, max_len - r);
  } else
    r = read(s->fd, buffer, max_len);

exit_fill_buffer_concat:
  return (r <= 0) ? -1 : r;
}

static int rar_open(char *rarname, mode_t m, stream_t *stream)
{
  stream_0day_priv_t *p;
  char RarTag[7], method, type = 0;
  unsigned short w, n, flag;

  int h = open(rarname, m);
  if (h < 0) return h;

  read(h, RarTag, 7);						/* Read Rar!... Tag */
  if (strncmp(RarTag, "Rar!", 4)) {
    lseek(h, 0, SEEK_SET);				/* Not a RAR */
    return h;
  }

  p = (stream_0day_priv_t*)malloc(sizeof(stream_0day_priv_t));
  memset(p, 0, sizeof(stream_0day_priv_t));
  p->headSize = 7;
  while (type != 0x74) {
    lseek(h, 2, SEEK_CUR);				/* CRC */
    read(h, &type, 1);					/* Type */
    read(h, (char*)&flag, 2);			/* Flag */
    read(h, (char*)&w, 2);				/* Size */
    p->headSize += w;
    if (type == 0x73) { /* main header */
      p->naming = flag & 0x10;
    } else
      if (type == 0x74) {	/* file header */
        read(h, (char*)&(p->packSize), 4);
        read(h, (char*)&(p->fileSize), 4);
        lseek(h, 10, SEEK_CUR);			/* Skip OS/CRC/Time/Ver */
        read(h, &method, 1);			/* Compression Method */
        read(h, (char*)&n, 2);			/* Size of rarname */
        if (w == n + 0x2D) {
          /* fileSize is 64bit */
          lseek(h, 8, SEEK_CUR);
          read(h, ((char*)&(p->fileSize))+4, 4);
        } else {
          lseek(h, 4, SEEK_CUR);		/* Attr */
        }
        p->filename = (char *)malloc(n + 1);
        read(h, p->filename, n);		/* filename */
        p->filename[n] = 0;
      } else
        if (type == 0x7A) {	/* comment header */
          read(h, (char*)&w, 2);			/* Size of comment */
          p->headSize += w;
        }
      lseek(h, p->headSize, SEEK_SET);	/* Seek to next header */
  }
  mp_msg(MSGT_STREAM,MSGL_INFO, "File Flags=%04x\tCompression Method=%x\n", flag, method);

  if (!(flag & 0x04) && (method == 0x30)) {	/* 0day stream */
    n = strlen(rarname);
    p->basename = strdup(rarname);
    if (p->naming) {
      p->naming = rarname + n - strrchr(rarname, 't') - 5;
      n -= (p->naming + 4);
    } else {
      n -= 2;
    }
    p->basename[n] = 0;

    close(h);
    h = open_0day_volume(p, 0);
    if (h < 0) {
      free(p->filename);
      free(p->basename);
      free(p);
    } else {
      /* reget packSize, avoid got the last volume's packSize */
      type = 0;
      n = 7;
      lseek(h, 7, SEEK_SET);
      while (type != 0x74) {
        lseek(h, 2, SEEK_CUR);				/* CRC */
        read(h, &type, 1);					/* Type */
        read(h, (char*)&flag, 2);			/* Flag */
        read(h, (char*)&w, 2);				/* Size */
        n += w;
        if (type == 0x74) {	/* file header */
          read(h, (char*)&(p->packSize), 4);
        } else
          if (type == 0x7A) {	/* comment header */
            read(h, (char*)&w, 2);			/* Size of comment */
            n += w;
          }
        lseek(h, n, SEEK_SET);	/* Seek to next header */
      }

      stream->priv = (void*)p;
      stream->close = close_0day;
      stream->seek = seek_0day;
      stream->fill_buffer = fill_buffer_0day;
      stream->end_pos = p->fileSize;
      stream->type = STREAMTYPE_FILE;
    }
    return h;
  }

  free(p);
  return h;
}

static int concat_open(char *rarname, mode_t m, stream_t *stream)
{
#define SLIPT_SIZE_FACTOR 15
  stream_0day_priv_t *p;
  unsigned long *rtmp;
  unsigned short mul;
  struct stat sb;
  unsigned short n;
  char *ext_of_filename;

  int h = open(rarname, m);
  if (h < 0) return h;

  p = (stream_0day_priv_t*)malloc(sizeof(stream_0day_priv_t));
  memset(p, 0, sizeof(stream_0day_priv_t));
  p->headSize = 0;

  if (fstat(h, &sb) != -1) {	/* get file stat */
    n = strlen(rarname);
    ext_of_filename = strrchr(rarname, '.');
    p->naming = strlen(ext_of_filename) - 1;
    p->basename = strdup(rarname);
    n -= p->naming;
    p->basename[n] = 0;
    p->filename = strdup(p->basename);
    p->packSize = sb.st_size;

    close(h);
    p->splitSize = (unsigned long*)malloc(sizeof(unsigned long) * SLIPT_SIZE_FACTOR);
    p->idx = 0;
    mul = 1;
    while ((h = open_0day_volume(p, p->idx)) >= 0) {
      if(p->idx >= (SLIPT_SIZE_FACTOR * mul)) {
        mul++;
        rtmp = (unsigned long*)realloc(p->splitSize, sizeof(unsigned long) * SLIPT_SIZE_FACTOR * mul);
	if (rtmp != NULL) {
          p->splitSize = rtmp;
	} else {
          mp_msg(MSGT_OPEN,MSGL_INFO,"[concat] Error: realloc failed\n");
          free(p->splitSize);
          free(p->filename);
          free(p->basename);
          free(p);
          close(h);
          return -1;
	}
      }
      fstat(h, &sb);
      p->fileSize += sb.st_size;
      p->splitSize[p->idx] = sb.st_size;
      if (p->packSize != sb.st_size)
        mp_msg(MSGT_OPEN,MSGL_INFO,"[concat] Warning: fileSize of %s%0*d is NOT eq to first volume\n", p->basename, p->naming, p->idx + 1);
      p->idx++;
      close(h);
    }
    rtmp = (unsigned long*)realloc(p->splitSize, sizeof(unsigned long) * p->idx);
    if (rtmp != NULL) {
      p->splitSize = rtmp;
    } else {
      mp_msg(MSGT_OPEN,MSGL_INFO,"[concat] Error: realloc failed\n");
      free(p->splitSize);
      free(p->filename);
      free(p->basename);
      free(p);
      return -1;
    }
    mp_msg(MSGT_OPEN,MSGL_INFO,"[concat] Info: total %d volumes\n", p->idx);
    p->idx=0;

    h = open_0day_volume(p, 0);
    if (h < 0) {
      free(p->splitSize);
      free(p->filename);
      free(p->basename);
      free(p);
    } else {
      stream->priv = (void*)p;
      stream->close = close_0day;
      stream->seek = seek_concat;
      stream->fill_buffer = fill_buffer_concat;
      stream->end_pos = p->fileSize;
      stream->type = STREAMTYPE_FILE;
    }
    return h;
  }

  free(p);
  return h;
}

static int fill_buffer(stream_t *s, char* buffer, int max_len){
  int r = read(s->fd,buffer,max_len);
  return (r <= 0) ? -1 : r;
}

static int write_buffer(stream_t *s, char* buffer, int len) {
  int r = write(s->fd,buffer,len);
  return (r <= 0) ? -1 : r;
}

static int seek(stream_t *s,off_t newpos) {
  s->pos = newpos;
  if(lseek(s->fd,s->pos,SEEK_SET)<0) {
    s->eof=1;
    return 0;
  }
  return 1;
}

static int seek_forward(stream_t *s,off_t newpos) {
  if(newpos<s->pos){
    mp_msg(MSGT_STREAM,MSGL_INFO,"Cannot seek backward in linear streams!\n");
    return 0;
  }
  while(s->pos<newpos){
    int len=s->fill_buffer(s,s->buffer,STREAM_BUFFER_SIZE);
    if(len<=0){ s->eof=1; s->buf_pos=s->buf_len=0; break; } // EOF
    s->buf_pos=0;
    s->buf_len=len;
    s->pos+=len;
  }
  return 1;
}

static int control(stream_t *s, int cmd, void *arg) {
  switch(cmd) {
    case STREAM_CTRL_GET_SIZE: {
      off_t size;

      if (s->priv) {
        if (s->type == STREAMTYPE_STREAM) return STREAM_UNSUPPORTED;
        size = ((stream_0day_priv_t*)s->priv)->fileSize;
      } else {
      size = lseek(s->fd, 0, SEEK_END);
      lseek(s->fd, s->pos, SEEK_SET);
      }
      if(size != (off_t)-1) {
        *((off_t*)arg) = size;
        return 1;
      }
    }
  }
  return STREAM_UNSUPPORTED;
}

static int open_f(stream_t *stream,int mode, void* opts, int* file_format) {
  int f;
  mode_t m = 0;
  off_t len;
  unsigned char *filename;
  struct stream_priv_s* p = (struct stream_priv_s*)opts;

  if(mode == STREAM_READ)
    m = O_RDONLY;
  else if(mode == STREAM_WRITE)
    m = O_RDWR|O_CREAT|O_TRUNC;
  else {
    mp_msg(MSGT_OPEN,MSGL_ERR, "[file] Unknown open mode %d\n",mode);
    m_struct_free(&stream_opts,opts);
    return STREAM_UNSUPPORTED;
  }

  if(p->filename)
    filename = p->filename;
  else if(p->filename2)
    filename = p->filename2;
  else
    filename = NULL;
  if(!filename) {
    mp_msg(MSGT_OPEN,MSGL_ERR, "[file] No filename\n");
    m_struct_free(&stream_opts,opts);
    return STREAM_ERROR;
  }

#if HAVE_DOS_PATHS
  // extract '/' from '/x:/path'
  if( filename[ 0 ] == '/' && filename[ 1 ] && filename[ 2 ] == ':' )
    filename++;
#endif

  m |= O_BINARY;

  if(!strcmp(filename,"-")){
    if(mode == STREAM_READ) {
      // read from stdin
      mp_msg(MSGT_OPEN,MSGL_INFO,MSGTR_ReadSTDIN);
      f=0; // 0=stdin
#if HAVE_SETMODE
      setmode(fileno(stdin),O_BINARY);
#endif
    } else {
      mp_msg(MSGT_OPEN,MSGL_INFO,"Writing to stdout\n");
      f=1;
#if HAVE_SETMODE
      setmode(fileno(stdout),O_BINARY);
#endif
    }
  } else {
      mode_t openmode = S_IRUSR|S_IWUSR;
#ifndef __MINGW32__
      openmode |= S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH;
#endif
      char *s = strrchr(filename,'.');
      if (s && (!strcasecmp(s,".rar") || (!strncasecmp(s,".r",2) && isdigit(s[2]) && isdigit(s[3]) && s[4]==0)))
      f=rar_open(filename,m,stream);
      else if (s && (!strcasecmp(s,".001")))
      f=concat_open(filename,m,stream);
      else
      f=open(filename,m, openmode);
    if(f<0) {
      mp_msg(MSGT_OPEN,MSGL_ERR,MSGTR_FileNotFound,filename);
      m_struct_free(&stream_opts,opts);
      return STREAM_ERROR;
    }
  }

if (!stream->end_pos)
{
  len=lseek(f,0,SEEK_END); lseek(f,0,SEEK_SET);
#ifdef __MINGW32__
  // seeks on stdin incorrectly succeed on MinGW
  if(f==0)
    len = -1;
#endif
  if(len == -1) {
    if(mode == STREAM_READ) stream->seek = seek_forward;
    stream->type = STREAMTYPE_STREAM; // Must be move to STREAMTYPE_FILE
    stream->flags |= MP_STREAM_SEEK_FW;
  } else if(len >= 0) {
    stream->seek = seek;
    stream->end_pos = len;
    stream->type = STREAMTYPE_FILE;
  }
  stream->fill_buffer = fill_buffer;
} else len = stream->end_pos;

  mp_msg(MSGT_OPEN,MSGL_INFO,"[file] File size is %"PRId64" bytes\n", (int64_t)len);

  stream->fd = f;
  stream->write_buffer = write_buffer;
  stream->control = control;

  m_struct_free(&stream_opts,opts);
  return STREAM_OK;
}

const stream_info_t stream_info_file = {
  "File",
  "file",
  "Albeu",
  "based on the code from ??? (probably Arpi)",
  open_f,
  { "file", "", NULL },
  &stream_opts,
  1 // Urls are an option string
};

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2001 Ximian, Inc. (www.ximian.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "gmime-stream-mmap.h"


static void stream_destroy (GMimeStream *stream);
static ssize_t stream_read (GMimeStream *stream, char *buf, size_t len);
static ssize_t stream_write (GMimeStream *stream, char *buf, size_t len);
static int stream_flush (GMimeStream *stream);
static int stream_close (GMimeStream *stream);
static gboolean stream_eos (GMimeStream *stream);
static int stream_reset (GMimeStream *stream);
static off_t stream_seek (GMimeStream *stream, off_t offset, GMimeSeekWhence whence);
static off_t stream_tell (GMimeStream *stream);
static ssize_t stream_length (GMimeStream *stream);
static GMimeStream *stream_substream (GMimeStream *stream, off_t start, off_t end);

static GMimeStream stream_template = {
	NULL, 0,
	1, 0, 0, 0, stream_destroy,
	stream_read, stream_write,
	stream_flush, stream_close,
	stream_eos, stream_reset,
	stream_seek, stream_tell,
	stream_length, stream_substream,
};


static void
stream_destroy (GMimeStream *stream)
{
	GMimeStreamMmap *mstream = (GMimeStreamMmap *) stream;
	
	if (mstream->owner) {
#ifdef HAVE_MUNMAP
		if (mstream->map)
			munmap (mstream->map, mstream->maplen);
#endif
		
		if (mstream->fd != -1)
			close (mstream->fd);
	}
	
	g_free (mstream);
}

static ssize_t
stream_read (GMimeStream *stream, char *buf, size_t len)
{
	GMimeStreamMmap *mstream = (GMimeStreamMmap *) stream;
	register char *mapptr;
	ssize_t nread;
	
	if (stream->bound_end != -1 && stream->position >= stream->bound_end)
		return -1;
	
	/* make sure we are at the right position */
	mapptr = mstream->map + stream->position;
	
	if (stream->bound_end == -1)
		nread = MIN ((mstream->map + mstream->maplen) - mapptr, len);
	else
		nread = MIN (stream->bound_end - stream->position, len);
	
	if (nread > 0) {
		memcpy (buf, mapptr, nread);
		stream->position += nread;
	} else
		mstream->eos = TRUE;
	
	return nread;
}

static ssize_t
stream_write (GMimeStream *stream, char *buf, size_t len)
{
	GMimeStreamMmap *mstream = (GMimeStreamMmap *) stream;
	register char *mapptr;
	ssize_t nwritten;
	
	if (stream->bound_end != -1 && stream->position >= stream->bound_end)
		return -1;
	
	/* make sure we are at the right position */
	mapptr = mstream->map + stream->position;
	
	if (stream->bound_end == -1)
		nwritten = MIN ((mstream->map + mstream->maplen) - mapptr, len);
	else
		nwritten = MIN (stream->bound_end - stream->position, len);
	
	if (nwritten > 0) {
		memcpy (mapptr, buf, nwritten);
		stream->position += nwritten;
	}
	
	return nwritten;
}

static int
stream_flush (GMimeStream *stream)
{
	GMimeStreamMmap *mstream = (GMimeStreamMmap *) stream;
	
	g_return_val_if_fail (mstream->fd != -1, -1);
	
#ifdef HAVE_MSYNC
	return msync (mstream->map, mstream->maplen, MS_SYNC /* | MS_INVALIDATE */);
#else
	return 0;
#endif
}

static int
stream_close (GMimeStream *stream)
{
	GMimeStreamMmap *mstream = (GMimeStreamMmap *) stream;
	int ret = 0;
	
	if (mstream->owner && mstream->map) {
#ifdef HAVE_MUNMAP
		munmap (mstream->map, mstream->maplen);
		mstream->map = NULL;
#endif
	}
	
	if (mstream->owner && mstream->fd != -1) {
		ret = close (mstream->fd);
		if (ret != -1)
			mstream->fd = -1;
	}
	
	return ret;
}

static gboolean
stream_eos (GMimeStream *stream)
{
	GMimeStreamMmap *mstream = (GMimeStreamMmap *) stream;
	
	g_return_val_if_fail (mstream->fd != -1, TRUE);
	
	return mstream->eos;
}

static int
stream_reset (GMimeStream *stream)
{
	GMimeStreamMmap *mstream = (GMimeStreamMmap *) stream;
	
	g_return_val_if_fail (mstream->fd != -1, -1);
	
	stream->position = stream->bound_start;
	mstream->eos = FALSE;
	
	return 0;
}

static off_t
stream_seek (GMimeStream *stream, off_t offset, GMimeSeekWhence whence)
{
	GMimeStreamMmap *mstream = (GMimeStreamMmap *) stream;
	off_t real = stream->position;
	
	g_return_val_if_fail (mstream->fd != -1, -1);
	
	switch (whence) {
	case GMIME_STREAM_SEEK_SET:
		real = offset;
		break;
	case GMIME_STREAM_SEEK_CUR:
		real = stream->position + offset;
		break;
	case GMIME_STREAM_SEEK_END:
		if (stream->bound_end == -1) {
			real = offset <= 0 ? stream->bound_start + mstream->maplen + offset: -1;
			if (real != -1) {
				if (real < stream->bound_start)
					real = stream->bound_start;
				stream->position = real;
			}
			
			return real;
		}
		real = stream->bound_end + offset;
		break;
	}
	
	if (stream->bound_end != -1)
		real = MIN (real, stream->bound_end);
	real = MAX (real, stream->bound_start);
	
	if (real != stream->position && mstream->eos)
		mstream->eos = FALSE;
	
	stream->position = real;
	
	return real;
}

static off_t
stream_tell (GMimeStream *stream)
{
	return stream->position;
}

static ssize_t
stream_length (GMimeStream *stream)
{
	GMimeStreamMmap *mstream = (GMimeStreamMmap *) stream;
	
	if (stream->bound_start != -1 && stream->bound_end != -1)
		return stream->bound_end - stream->bound_start;
	
	return mstream->maplen;
}

static GMimeStream *
stream_substream (GMimeStream *stream, off_t start, off_t end)
{
	/* FIXME: maybe we should return a GMimeStreamFs? */
	GMimeStreamMmap *mstream;
	
	mstream = g_new (GMimeStreamMmap, 1);
	mstream->owner = FALSE;
	mstream->fd = GMIME_STREAM_MMAP (stream)->fd;
	
	mstream->map = GMIME_STREAM_MMAP (stream)->map;
	mstream->maplen = GMIME_STREAM_MMAP (stream)->maplen;
	
	g_mime_stream_construct (GMIME_STREAM (mstream), &stream_template, GMIME_STREAM_MMAP_TYPE, start, end);
	
	return GMIME_STREAM (mstream);
}


/**
 * g_mime_stream_mmap_new:
 * @fd: file descriptor
 * @prot: protection flags
 * @flags: map flags
 *
 * Creates a new GMimeStreamMmap object around @fd.
 *
 * Returns a stream using @fd.
 **/
GMimeStream *
g_mime_stream_mmap_new (int fd, int prot, int flags)
{
#ifdef HAVE_MMAP
	GMimeStreamMmap *mstream;
	struct stat st;
	off_t start;
	char *map;
	
	start = lseek (fd, 0, SEEK_CUR);
	if (start == -1)
		return NULL;
	
	if (fstat (fd, &st) == -1)
		return NULL;
	
	map = mmap (NULL, st.st_size + getpagesize (), prot, flags, fd, 0);
	if (map == MAP_FAILED)
		return NULL;
	
	mstream = g_new (GMimeStreamMmap, 1);
	mstream->owner = TRUE;
	mstream->eos = FALSE;
	mstream->fd = fd;
	mstream->map = map;
	mstream->maplen = st.st_size;
	
	g_mime_stream_construct (GMIME_STREAM (mstream), &stream_template, GMIME_STREAM_MMAP_TYPE, start, -1);
	
	return GMIME_STREAM (mstream);
#else
	return NULL;
#endif /* HAVE_MMAP */
}


/**
 * g_mime_stream_mmap_new_with_bounds:
 * @fd: file descriptor
 * @prot: protection flags
 * @flags: map flags
 * @start: start boundary
 * @end: end boundary
 *
 * Creates a new GMimeStreamMmap object around @fd with bounds @start
 * and @end.
 *
 * Returns a stream using @fd with bounds @start and @end.
 **/
GMimeStream *
g_mime_stream_mmap_new_with_bounds (int fd, int prot, int flags, off_t start, off_t end)
{
#ifdef HAVE_MMAP
	GMimeStreamMmap *mstream;
	struct stat st;
	char *map;
	
	if (end == -1) {
		if (fstat (fd, &st) == -1)
			return NULL;
	} else
		st.st_size = end /* - start */;
	
	map = mmap (NULL, st.st_size + getpagesize (), prot, flags, fd, 0);
	if (map == MAP_FAILED)
		return NULL;
	
	mstream = g_new (GMimeStreamMmap, 1);
	mstream->owner = TRUE;
	mstream->eos = FALSE;
	mstream->fd = fd;
	mstream->map = map;
	mstream->maplen = st.st_size;
	
	g_mime_stream_construct (GMIME_STREAM (mstream), &stream_template, GMIME_STREAM_MMAP_TYPE, start, end);
	
	return GMIME_STREAM (mstream);
#else
	return NULL;
#endif /* HAVE_MMAP */
}
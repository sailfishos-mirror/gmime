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


#ifndef __GMIME_STREAM_FILTER_H__
#define __GMIME_STREAM_FILTER_H__

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#include "gmime-stream.h"
#include "gmime-filter.h"

typedef struct _GMimeStreamFilter {
	GMimeStream parent;
	
	struct _GMimeStreamFilterPrivate *priv;
	
	GMimeStream *source;
} GMimeStreamFilter;

#define GMIME_STREAM_FILTER_TYPE g_str_hash ("GMimeStreamFilter")
#define GMIME_IS_STREAM_FILTER(stream) (((GMimeStream *) stream)->type == GMIME_STREAM_FILTER_TYPE)
#define GMIME_STREAM_FILTER(stream) ((GMimeStreamFilter *) stream)

GMimeStream *g_mime_stream_filter_new_with_stream (GMimeStream *stream);

int g_mime_stream_filter_add (GMimeStreamFilter *fstream, GMimeFilter *filter);
void g_mime_stream_filter_remove (GMimeStreamFilter *fstream, int id);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GMIME_STREAM_FILTER_H__ */
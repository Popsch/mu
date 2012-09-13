/* -*-mode: c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-*/
/*
** Copyright (C) 2008-2012 Dirk-Jan C. Binnema <djcb@djcbsoftware.nl>
**
** This program is free software; you can redistribute it and/or modify it
** under the terms of the GNU General Public License as published by the
** Free Software Foundation; either version 3, or (at your option) any
** later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation,
** Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
**
*/

#if HAVE_CONFIG_H
#include "config.h"
#endif /*HAVE_CONFIG_H*/

#include <string.h>
#include <unistd.h>

#include "mu-util.h"
#include "mu-str.h"
#include "mu-msg-priv.h"
#include "mu-msg-part.h"

static gboolean handle_children (MuMsg *msg,
				 GMimeMessage *mime_msg, MuMsgOptions opts,
				 unsigned index, MuMsgPartForeachFunc func,
				 gpointer user_data);
struct _DoData {
	GMimeObject *mime_obj;
	unsigned    index;
};
typedef struct _DoData DoData;

static void
do_it_with_index (MuMsg *msg, MuMsgPart *part, DoData *ddata)
{
	if (ddata->mime_obj)
		return;

	if (part->index == ddata->index)
		ddata->mime_obj = (GMimeObject*)part->data;
}

static GMimeObject*
get_mime_object_at_index (MuMsg *msg, MuMsgOptions opts, unsigned index)
{
	DoData ddata;

	ddata.mime_obj  = NULL;
	ddata.index     = index;

	mu_msg_part_foreach (msg, opts,
			     (MuMsgPartForeachFunc)do_it_with_index,
			     &ddata);

	return ddata.mime_obj;
}


typedef gboolean (*MuMsgPartMatchFunc) (MuMsgPart *, gpointer);
struct _MatchData {
	MuMsgPartMatchFunc match_func;
	gpointer user_data;
	int index;
};
typedef struct _MatchData MatchData;

static void
check_match (MuMsg *msg, MuMsgPart *part, MatchData *mdata)
{
	if (mdata->index != -1)
		return;

	if (mdata->match_func (part, mdata->user_data))
		mdata->index = part->index;
}

static int
get_matching_part_index (MuMsg *msg, MuMsgOptions opts,
			 MuMsgPartMatchFunc func, gpointer user_data)
{
	MatchData mdata;

	mdata.match_func = func;
	mdata.user_data  = user_data;
	mdata.index      = -1;

	mu_msg_part_foreach (msg, opts,
			     (MuMsgPartForeachFunc)check_match,
			     &mdata);
	return mdata.index;
}


static void
accumulate_text_message (MuMsg *msg, MuMsgPart *part, GString **gstrp)
{
	const gchar *str;
	char *adrs;
	GMimeMessage *mimemsg;
	InternetAddressList *addresses;

	/* put sender, recipients and subject in the string, so they
	 * can be indexed as well */
	mimemsg = GMIME_MESSAGE (part->data);
	str = g_mime_message_get_sender (mimemsg);
	g_string_append_printf
		(*gstrp, "%s%s", str ? str : "", str ? "\n" : "");
	str = g_mime_message_get_subject (mimemsg);
	g_string_append_printf
		(*gstrp, "%s%s", str ? str : "", str ? "\n" : "");
		addresses = g_mime_message_get_all_recipients (mimemsg);
		adrs = internet_address_list_to_string (addresses, FALSE);
		g_object_unref (addresses);
		g_string_append_printf
			(*gstrp, "%s%s", adrs ? adrs : "", adrs ? "\n" : "");
		g_free (adrs);
}

static void
accumulate_text_part (MuMsg *msg, MuMsgPart *part, GString **gstrp)
{
	GMimeContentType *ctype;
	gboolean err;
	char *txt;
	ctype = g_mime_object_get_content_type ((GMimeObject*)part->data);
	if (!g_mime_content_type_is_type (ctype, "text", "plain"))
		return; /* not plain text */
	txt = mu_msg_mime_part_to_string
		((GMimePart*)part->data, &err);
	if (txt)
		g_string_append (*gstrp, txt);
	g_free (txt);
}

static void
accumulate_text (MuMsg *msg, MuMsgPart *part, GString **gstrp)
{
	if (GMIME_IS_MESSAGE(part->data))
		accumulate_text_message (msg, part, gstrp);
	else if (GMIME_IS_PART (part->data))
		accumulate_text_part (msg, part, gstrp);
}

char*
mu_msg_part_get_text (MuMsg *msg, MuMsgPart *self, MuMsgOptions opts)
{
	GMimeObject  *mobj;
	GMimeMessage *mime_msg;
	gboolean err;

	g_return_val_if_fail (msg, NULL);
	g_return_val_if_fail (self && self->data, NULL);

	mobj = (GMimeObject*)self->data;

	err = FALSE;
	if (GMIME_IS_PART (mobj)) {
		if (self->part_type & MU_MSG_PART_TYPE_TEXT_PLAIN)
			return mu_msg_mime_part_to_string ((GMimePart*)mobj,
							   &err);
		else
			return NULL; /* non-text MimePart */
	}

	mime_msg = NULL;
	if (GMIME_IS_MESSAGE_PART (mobj))
		mime_msg = g_mime_message_part_get_message
			((GMimeMessagePart*)mobj);
	else if (GMIME_IS_MESSAGE (mobj))
		mime_msg = (GMimeMessage*)mobj;

	if (mime_msg) {
		GString *gstr;
		gstr = g_string_sized_new (4096);
		handle_children (msg, mime_msg, opts, self->index,
 				 (MuMsgPartForeachFunc)accumulate_text,
				 &gstr);
		return g_string_free (gstr, FALSE);
	} else {
		g_warning ("%s: cannot get text for %s",
			   __FUNCTION__, G_OBJECT_TYPE_NAME (mobj));
		return NULL;
	}
}


/* note: this will return -1 in case of error or if the size is
 * unknown */
static ssize_t
get_part_size (GMimePart *part)
{
	GMimeDataWrapper *wrapper;
	GMimeStream *stream;

	wrapper = g_mime_part_get_content_object (part);
	if (!GMIME_IS_DATA_WRAPPER(wrapper))
		return -1;

	stream = g_mime_data_wrapper_get_stream (wrapper);
	if (!stream)
		return -1; /* no stream -> size is 0 */
	else
		return g_mime_stream_length (stream);

	/* NOTE: stream/wrapper are owned by gmime, no unreffing */
}


static void
cleanup_filename (char *fname)
{
	gchar *cur;

	/* remove slashes, spaces, colons... */
	for (cur = fname; *cur; ++cur)
		if (*cur == '/' || *cur == ' ' || *cur == ':')
			*cur = '-';
}


static char*
mime_part_get_filename (GMimeObject *mobj, unsigned index,
			gboolean construct_if_needed)
{
	gchar *fname;

	fname = NULL;

	if (GMIME_IS_PART (mobj)) {
		/* the easy case: the part has a filename */
		fname = (gchar*)g_mime_part_get_filename (GMIME_PART(mobj));
		if (fname) /* don't include directory components */
			fname = g_path_get_basename (fname);
	}

	if (!fname && !construct_if_needed)
		return NULL;

	if (GMIME_IS_MESSAGE_PART(mobj)) {
		GMimeMessage *msg;
		const char *subj;
		msg  = g_mime_message_part_get_message
			(GMIME_MESSAGE_PART(mobj));
		subj = g_mime_message_get_subject (msg);
		fname = g_strdup_printf ("%s.eml", subj ? subj : "message");
	}

	if (!fname)
		fname =	g_strdup_printf ("%u.part", index);

	/* remove slashes, spaces, colons... */
	cleanup_filename (fname);
	return fname;
}


char*
mu_msg_part_get_filename (MuMsgPart *mpart, gboolean construct_if_needed)
{
	g_return_val_if_fail (mpart, NULL);
	g_return_val_if_fail (GMIME_IS_OBJECT(mpart->data), NULL);

	return mime_part_get_filename ((GMimeObject*)mpart->data,
				       mpart->index, construct_if_needed);
}


static MuMsgPartType
get_disposition (GMimeObject *mobj)
{
	const char *disp;

	disp = g_mime_object_get_disposition (mobj);
	if (!disp)
		return MU_MSG_PART_TYPE_NONE;

	if (strcasecmp (disp, GMIME_DISPOSITION_ATTACHMENT) == 0)
		return MU_MSG_PART_TYPE_ATTACHMENT;

	if (strcasecmp (disp, GMIME_DISPOSITION_INLINE) == 0)
		return MU_MSG_PART_TYPE_INLINE;

	return MU_MSG_PART_TYPE_NONE;
}

#define SIG_STATUS_REPORT "sig-status-report"

/* call 'func' with information about this MIME-part */
static gboolean
check_signature (MuMsg *msg, GMimeMultipartSigned *part, MuMsgOptions opts)
{
#ifdef BUILD_CRYPTO
	/* the signature status */
	MuMsgPartSigStatusReport *sigrep;
	GError *err;

	err     = NULL;
	sigrep = mu_msg_crypto_verify_part (part, opts, &err);
	if (err) {
		g_warning ("error verifying signature: %s", err->message);
		g_clear_error (&err);
	}

	/* tag this part with the signature status check */
	g_object_set_data_full
		(G_OBJECT(part), SIG_STATUS_REPORT,
		 sigrep,
		 (GDestroyNotify)mu_msg_part_sig_status_report_destroy);
#endif /*BUILD_CRYPTO*/
	return TRUE;
}

/* declaration, so we can use it in handle_encrypted_part */
static gboolean handle_mime_object (MuMsg *msg,
				    GMimeObject *mobj, GMimeObject *parent,
				    MuMsgOptions opts,
				    unsigned index, MuMsgPartForeachFunc func,
				    gpointer user_data);

/* call 'func' with information about this MIME-part */
static gboolean
handle_encrypted_part (MuMsg *msg,
		       GMimeMultipartEncrypted *part, GMimeObject *parent,
		       MuMsgOptions opts, unsigned index,
		       MuMsgPartForeachFunc func, gpointer user_data)
{
#ifdef BUILD_CRYPTO
	GError *err;
	GMimeObject *dec;

	err = NULL;
	dec = mu_msg_crypto_decrypt_part (part, opts, NULL, NULL, &err);
	if (err) {
		g_warning ("error decrypting part: %s", err->message);
		g_clear_error (&err);
	}

	if (dec) {
		gboolean rv;
		rv = handle_mime_object (msg, dec, parent, opts,
					 index + 1, func, user_data);
		g_object_unref (dec);
		return rv;
	}

#endif /*BUILD_CRYPTO*/
	return TRUE;
}


/* call 'func' with information about this MIME-part */
static gboolean
handle_part (MuMsg *msg, GMimePart *part, GMimeObject *parent,
	     MuMsgOptions opts, unsigned index,
	     MuMsgPartForeachFunc func, gpointer user_data)
{
	GMimeContentType *ct;
	MuMsgPart msgpart;

	memset (&msgpart, 0, sizeof(MuMsgPart));

	msgpart.size        = get_part_size (part);
	msgpart.part_type   = MU_MSG_PART_TYPE_LEAF;
	msgpart.part_type |= get_disposition ((GMimeObject*)part);

	ct = g_mime_object_get_content_type ((GMimeObject*)part);
	if (GMIME_IS_CONTENT_TYPE(ct)) {
		msgpart.type    = g_mime_content_type_get_media_type (ct);
		msgpart.subtype = g_mime_content_type_get_media_subtype (ct);
		/* store as in the part_type as well, for quick
		 * checking */
		if (g_mime_content_type_is_type (ct, "text", "plain"))
			msgpart.part_type |= MU_MSG_PART_TYPE_TEXT_PLAIN;
		else if (g_mime_content_type_is_type (ct, "text", "html"))
			msgpart.part_type |= MU_MSG_PART_TYPE_TEXT_HTML;
	}

	/* put the verification info in the pgp-signature part */
	msgpart.sig_status_report = NULL;
	if (g_ascii_strcasecmp (msgpart.subtype, "pgp-signature") == 0)
		msgpart.sig_status_report =
			(MuMsgPartSigStatusReport*)
			g_object_get_data (G_OBJECT(parent), SIG_STATUS_REPORT);

	msgpart.data    = (gpointer)part;
	msgpart.index   = index;

	func (msg, &msgpart, user_data);

	return TRUE;
}


/* call 'func' with information about this MIME-part */
static gboolean
handle_message_part (MuMsg *msg, GMimeMessagePart *mmsg, GMimeObject *parent,
		     MuMsgOptions opts, unsigned index,
		     MuMsgPartForeachFunc func, gpointer user_data)
{
	MuMsgPart msgpart;
	memset (&msgpart, 0, sizeof(MuMsgPart));

	msgpart.type        = "message";
	msgpart.subtype     = "rfc822";
	msgpart.index       = index;

	/* msgpart.size        = 0; /\* maybe calculate this? *\/ */

	msgpart.part_type  = MU_MSG_PART_TYPE_MESSAGE;
	msgpart.part_type |= get_disposition ((GMimeObject*)mmsg);

	msgpart.data        = (gpointer)mmsg;

	func (msg, &msgpart, user_data);

	if (opts & MU_MSG_OPTION_RECURSE_RFC822)
		return handle_children
			(msg, g_mime_message_part_get_message (mmsg),
			 opts, index, func, user_data);

	return TRUE;
}


static gboolean
handle_mime_object (MuMsg *msg,
		    GMimeObject *mobj, GMimeObject *parent, MuMsgOptions opts,
		    unsigned index, MuMsgPartForeachFunc func, gpointer user_data)
{
	if (GMIME_IS_PART (mobj))
		return handle_part
			(msg, GMIME_PART(mobj), parent,
			 opts, index, func, user_data);
	else if (GMIME_IS_MESSAGE_PART (mobj))
		return handle_message_part
			(msg, GMIME_MESSAGE_PART(mobj),
			 parent, opts, index, func, user_data);
	else if ((opts & MU_MSG_OPTION_VERIFY) &&
		 GMIME_IS_MULTIPART_SIGNED (mobj))
		return check_signature
			(msg, GMIME_MULTIPART_SIGNED (mobj), opts);
	else if ((opts & MU_MSG_OPTION_DECRYPT) &&
		GMIME_IS_MULTIPART_ENCRYPTED (mobj))
		return handle_encrypted_part
			(msg, GMIME_MULTIPART_ENCRYPTED (mobj),
			 parent, opts, index, func, user_data);
	return TRUE;
}

struct _ForeachData {
	MuMsgPartForeachFunc func;
	gpointer             user_data;
	MuMsg                *msg;
	unsigned             index;
	MuMsgOptions         opts;

};
typedef struct _ForeachData ForeachData;

static void
each_child (GMimeObject *parent, GMimeObject *part,
	    ForeachData *fdata)
{
	handle_mime_object (fdata->msg,
			    part,
			    parent,
			    fdata->opts,
			    fdata->index++,
			    fdata->func,
			    fdata->user_data);
}


static gboolean
handle_children (MuMsg *msg,
		 GMimeMessage *mime_msg, MuMsgOptions opts,
		 unsigned index, MuMsgPartForeachFunc func,
		 gpointer user_data)
{
	ForeachData fdata;

 	fdata.func	= func;
	fdata.user_data = user_data;
	fdata.opts	= opts;
	fdata.msg	= msg;
	fdata.index	= 0;

	g_mime_message_foreach (mime_msg, (GMimeObjectForeachFunc)each_child,
				&fdata);

	return TRUE;
}


gboolean
mu_msg_part_foreach (MuMsg *msg, MuMsgOptions opts,
		     MuMsgPartForeachFunc func, gpointer user_data)
{
	GMimeMessage *mime_msg;
	unsigned idx;

	g_return_val_if_fail (msg, FALSE);

	if (!mu_msg_load_msg_file (msg, NULL))
		return FALSE;

	idx = 0;
	mime_msg = GMIME_MESSAGE(msg->_file->_mime_msg);

	return handle_children (msg, mime_msg, opts, idx, func, user_data);
}


gboolean
write_part_to_fd (GMimePart *part, int fd, GError **err)
{
	GMimeStream *stream;
	GMimeDataWrapper *wrapper;
	gboolean rv;

	stream = g_mime_stream_fs_new (fd);
	if (!GMIME_IS_STREAM(stream)) {
		g_set_error (err, MU_ERROR_DOMAIN, MU_ERROR_GMIME,
			     "failed to create stream");
		return FALSE;
	}
	g_mime_stream_fs_set_owner (GMIME_STREAM_FS(stream), FALSE);

	wrapper = g_mime_part_get_content_object (part);
	if (!GMIME_IS_DATA_WRAPPER(wrapper)) {
		g_set_error (err, MU_ERROR_DOMAIN, MU_ERROR_GMIME,
			     "failed to create wrapper");
		g_object_unref (stream);
		return FALSE;
	}
	g_object_ref (part); /* FIXME: otherwise, the unrefs below
			      * give errors...*/

	if (g_mime_data_wrapper_write_to_stream (wrapper, stream) == -1) {
		rv = FALSE;
		g_set_error (err, MU_ERROR_DOMAIN, MU_ERROR_GMIME,
			     "failed to write to stream");
	} else
		rv = TRUE;

	g_object_unref (wrapper);
	g_object_unref (stream);

	return rv;
}



static gboolean
write_object_to_fd (GMimeObject *obj, int fd, GError **err)
{
	gchar *str;
	str = g_mime_object_to_string (obj);

	if (!str) {
		g_set_error (err, MU_ERROR_DOMAIN, MU_ERROR_GMIME,
			     "could not get string from object");
		return FALSE;
	}

	if (write (fd, str, strlen(str)) == -1) {
		g_set_error (err, MU_ERROR_DOMAIN, MU_ERROR_GMIME,
			     "failed to write object: %s",
			     strerror(errno));
		return FALSE;
	}

	return TRUE;
}


static gboolean
save_object (GMimeObject *obj, MuMsgOptions opts, const char *fullpath,
	     GError **err)
{
	int fd;
	gboolean rv;
	gboolean use_existing, overwrite;

	use_existing = opts & MU_MSG_OPTION_USE_EXISTING;
	overwrite    = opts & MU_MSG_OPTION_OVERWRITE;

	/* don't try to overwrite when we already have it; useful when
	 * you're sure it's not a different file with the same name */
	if (use_existing && access (fullpath, F_OK) == 0)
		return TRUE;

	/* ok, try to create the file */
	fd = mu_util_create_writeable_fd (fullpath, 0600, overwrite);
	if (fd == -1) {
		g_set_error (err, MU_ERROR_DOMAIN, MU_ERROR_FILE,
			     "could not open '%s' for writing: %s",
			     fullpath, errno ? strerror(errno) : "error");
		return FALSE;
	}

	if (GMIME_IS_PART (obj))
		rv = write_part_to_fd ((GMimePart*)obj, fd, err);
	else
		rv = write_object_to_fd (obj, fd, err);

	if (close (fd) != 0 && !err) { /* don't write on top of old err */
		g_set_error (err, MU_ERROR_DOMAIN, MU_ERROR_FILE,
			     "could not close '%s': %s",
			     fullpath, errno ? strerror(errno) : "error");
		return FALSE;
	}

	return rv;
}


gchar*
mu_msg_part_get_path (MuMsg *msg, MuMsgOptions opts,
		      const char* targetdir, unsigned index, GError **err)
{
	char *fname, *filepath;
	GMimeObject* mobj;

	g_return_val_if_fail (msg, NULL);

	if (!mu_msg_load_msg_file (msg, NULL))
		return NULL;

	mobj = get_mime_object_at_index (msg, opts, index);
	if (!mobj){
		mu_util_g_set_error (err, MU_ERROR_GMIME,
				     "cannot find part %u", index);
		return NULL;
	}

	fname = mime_part_get_filename (mobj, index, TRUE);
	filepath = g_build_path (G_DIR_SEPARATOR_S, targetdir ? targetdir : "",
				 fname, NULL);
	g_free (fname);

	return filepath;
}



gchar*
mu_msg_part_get_cache_path (MuMsg *msg, MuMsgOptions opts, guint partid,
			    GError **err)
{
	char *dirname, *filepath;
	const char* path;

	g_return_val_if_fail (msg, NULL);

	if (!mu_msg_load_msg_file (msg, NULL))
		return NULL;

	path = mu_msg_get_path (msg);

	/* g_compute_checksum_for_string may be better, but requires
	 * rel. new glib (2.16) */
        dirname = g_strdup_printf ("%s%c%x%c%u",
				   mu_util_cache_dir(), G_DIR_SEPARATOR,
				   g_str_hash (path), G_DIR_SEPARATOR,
				   partid);

	if (!mu_util_create_dir_maybe (dirname, 0700, FALSE)) {
		mu_util_g_set_error (err, MU_ERROR_FILE,
				     "failed to create dir %s", dirname);
		g_free (dirname);
		return NULL;
	}

	filepath = mu_msg_part_get_path (msg, opts, dirname, partid, err);
	g_free (dirname);

	return filepath;
}


gboolean
mu_msg_part_save (MuMsg *msg, MuMsgOptions opts,
		  const char *fullpath, guint partidx, GError **err)
{
	GMimeObject *part;

	g_return_val_if_fail (msg, FALSE);
	g_return_val_if_fail (fullpath, FALSE);
	g_return_val_if_fail (!((opts & MU_MSG_OPTION_OVERWRITE) &&
			        (opts & MU_MSG_OPTION_USE_EXISTING)), FALSE);

	if (!mu_msg_load_msg_file (msg, err))
		return FALSE;

	part = get_mime_object_at_index (msg, opts, partidx);
	if (!GMIME_IS_PART(part) || GMIME_IS_MESSAGE_PART(part)) {
		g_set_error (err, MU_ERROR_DOMAIN, MU_ERROR_GMIME,
			     "unexpected type %s for part %u",
			     G_OBJECT_TYPE_NAME((GObject*)part),
			     partidx);
		return FALSE;
	}

	return save_object (part, opts, fullpath, err);
}


gchar*
mu_msg_part_save_temp (MuMsg *msg, MuMsgOptions opts, guint partidx, GError **err)
{
	gchar *filepath;

	filepath = mu_msg_part_get_cache_path (msg, opts, partidx, err);
	if (!filepath)
		return NULL;

	if (!mu_msg_part_save (msg, opts, filepath, partidx, err)) {
		g_free (filepath);
		return NULL;
	}

	return filepath;
}

static gboolean
match_cid (MuMsgPart *mpart, const char *cid)
{
	const char *this_cid;

	this_cid = g_mime_object_get_content_id ((GMimeObject*)mpart->data);

	return g_strcmp0 (this_cid, cid) ? TRUE : FALSE;
}

int
mu_msg_find_index_for_cid (MuMsg *msg, MuMsgOptions opts, const char *sought_cid)
{
	const char* cid;

	g_return_val_if_fail (msg, -1);
	g_return_val_if_fail (sought_cid, -1);

	if (!mu_msg_load_msg_file (msg, NULL))
		return -1;

	cid = g_str_has_prefix (sought_cid, "cid:") ?
		sought_cid + 4 : sought_cid;

	return get_matching_part_index (msg, opts,
					(MuMsgPartMatchFunc)match_cid,
					(gpointer)(char*)cid);
}

struct _RxMatchData {
 	GSList       *_lst;
	const GRegex *_rx;
	guint         _idx;
};
typedef struct _RxMatchData RxMatchData;


static void
match_filename_rx (MuMsg *msg, MuMsgPart *mpart, RxMatchData *mdata)
{
	char *fname;

	fname = mu_msg_part_get_filename (mpart, FALSE);
	if (!fname)
		return;

	if (g_regex_match (mdata->_rx, fname, 0, NULL))
		mdata->_lst = g_slist_prepend (mdata->_lst,
					       GUINT_TO_POINTER(mpart->index));
	g_free (fname);
}


GSList*
mu_msg_find_files (MuMsg *msg, MuMsgOptions opts, const GRegex *pattern)
{
	RxMatchData mdata;

	g_return_val_if_fail (msg, NULL);
	g_return_val_if_fail (pattern, NULL);

	if (!mu_msg_load_msg_file (msg, NULL))
		return NULL;

	mdata._lst = NULL;
	mdata._rx  = pattern;
	mdata._idx = 0;

	mu_msg_part_foreach (msg, opts,
			     (MuMsgPartForeachFunc)match_filename_rx,
			     &mdata);
	return mdata._lst;
}


gboolean
mu_msg_part_maybe_attachment (MuMsgPart *part)
{
	g_return_val_if_fail (part, FALSE);

	/* attachments must be leaf parts */
	if (!part->part_type && MU_MSG_PART_TYPE_LEAF)
		return FALSE;

	/* non-textual inline parts are considered attachments as
	 * well */
	if (part->part_type & MU_MSG_PART_TYPE_INLINE &&
	    !(part->part_type & MU_MSG_PART_TYPE_TEXT_PLAIN) &&
	    !(part->part_type & MU_MSG_PART_TYPE_TEXT_HTML))
		return TRUE;

	return FALSE;
}

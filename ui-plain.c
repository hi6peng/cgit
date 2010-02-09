/* ui-plain.c: functions for output of plain blobs by path
 *
 * Copyright (C) 2008 Lars Hjemli
 *
 * Licensed under GNU General Public License v2
 *   (see COPYING for full license text)
 */

#include "cgit.h"
#include "html.h"
#include "ui-shared.h"

int match_baselen;
int match;

static void not_found()
{
	html("Status: 404 Not Found\n"
	     "Content-Type: text/html; charset=UTF-8\n"
	     "\n"
	     "<html><head><title>404 Not Found</title></head>\n"
	     "<body><h1>404 Not Found</h1></body></html>\n");
}

static int ensure_slash()
{
	size_t n;
	char *path;
	if (!ctx.cfg.virtual_root || !ctx.env.script_name
	    || !ctx.env.path_info)
		return 1;
	n = strlen(ctx.env.path_info);
	if (ctx.env.path_info[n-1] == '/')
		return 1;
	path = fmt("%s%s%s%s/", cgit_httpscheme(), cgit_hosturl(),
		   ctx.env.script_name,
		   ctx.env.path_info);
	html("Status: 301 Moved Permanently\n"
	     "Location: ");
	html(path);
	html("\n"
	     "Content-Type: text/html; charset=UTF-8\n"
	     "\n"
	     "<html><head><title>301 Moved Permanently</title></head>\n"
	     "<body><h1>404 Not Found</h1>\n"
	     "<p>The document has moved <a href='");
	html_url_path(path);
	html("'>here</a>.\n"
	     "</body></html>\n");
	return 0;
}

static int do_any_refs_exist(const char *refname, const unsigned char *sha1,
			     int flags, void *cb_data)
{
	return 1;
}

static int print_reflist(const char *refname, const unsigned char *sha1,
			 int flags, void *cb_data)
{
	html("  <li><a href='");
	html_url_path(refname);
	html("/'>");
	html_txt(refname);
	html("</a></li>\n");
	return 0;
}

static void print_branches()
{
	if (!ensure_slash())
		return;

	cgit_print_http_headers(&ctx);
	html("<html><head><title>");
	html_txt(ctx.repo->name);
	html("</title></head>\n<body>\n <h2>");
	html_txt(ctx.repo->name);
	html("</h2>\n");

	html(" <p><a href='HEAD/'>HEAD</a></p>\n");

	if (for_each_branch_ref(do_any_refs_exist, NULL)) {
		html(" <p>Branches:</p>\n <ul>\n");
		for_each_branch_ref(print_reflist, NULL);
		html(" </ul>\n");
	}
	else
		html(" <p>(no branches)</p>\n");

	if (for_each_tag_ref(do_any_refs_exist, NULL)) {
		html(" <p>Tags:</p>\n <ul>\n");
		for_each_tag_ref(print_reflist, NULL);
		html(" </ul>\n");
	}
	else
		html(" <p>(no tags)</p>\n");

	if (ctx.repo->enable_remote_branches) {
		if (for_each_remote_ref(do_any_refs_exist, NULL)) {
			html(" <p>Remote Branches:</p>\n <ul>\n");
			for_each_remote_ref(print_reflist, NULL);
			html(" </ul>\n");
		}
		else
			html(" <p>(no remote branches)</p>\n");
	}
}

static void print_object(const unsigned char *sha1, const char *path)
{
	enum object_type type;
	char *buf, *ext;
	unsigned long size;
	struct string_list_item *mime;

	type = sha1_object_info(sha1, &size);
	if (type == OBJ_BAD) {
		not_found();
		return;
	}

	buf = read_sha1_file(sha1, &type, &size);
	if (!buf) {
		not_found();
		return;
	}
	ctx.page.mimetype = NULL;
	ext = strrchr(path, '.');
	if (ext && *(++ext)) {
		mime = string_list_lookup(ext, &ctx.cfg.mimetypes);
		if (mime)
			ctx.page.mimetype = (char *)mime->util;
	}
	if (!ctx.page.mimetype) {
		if (buffer_is_binary(buf, size))
			ctx.page.mimetype = "application/octet-stream";
		else
			ctx.page.mimetype = "text/plain";
	}
	ctx.page.filename = fmt("%s", path);
	ctx.page.size = size;
	ctx.page.etag = sha1_to_hex(sha1);
	cgit_print_http_headers(&ctx);
	html_raw(buf, size);
	match = 1;
}

static int print_dir(const unsigned char *sha1, const char *path,
		      const char *base)
{
	char *fullpath;
	if (!ensure_slash())
		return 0;
	if (path[0] || base[0])
		fullpath = fmt("/%s%s/", base, path);
	else
		fullpath = "/";
	ctx.page.etag = sha1_to_hex(sha1);
	cgit_print_http_headers(&ctx);
	html("<html><head><title>");
	html_txt(fullpath);
	html("</title></head>\n<body>\n <h2>");
	html_txt(fullpath);
	html("</h2>\n <ul>\n");
	if (path[0] || base[0])
	      html("  <li><a href=\"../\">../</a></li>\n");
	match = 2;
	return 1;
}

static void print_dir_entry(const unsigned char *sha1, const char *path,
			    unsigned mode)
{
	char *url;
	if (S_ISDIR(mode))
		url = fmt("%s/", path);
	else
		url = fmt("%s", path);
	html("  <li><a href='");
	html_url_path(url);
	html("'>");
	html_txt(url);
	html("</a></li>\n");
	match = 2;
}

static void print_dir_tail(void)
{
	html(" </ul>\n</body></html>\n");
}

static int walk_tree(const unsigned char *sha1, const char *base, int baselen,
		     const char *pathname, unsigned mode, int stage,
		     void *cbdata)
{
	if (baselen == match_baselen) {
		if (S_ISREG(mode))
			print_object(sha1, pathname);
		else if (S_ISDIR(mode)) {
			if (print_dir(sha1, pathname, base))
				return READ_TREE_RECURSIVE;
		}
	}
	else if (baselen > match_baselen)
		print_dir_entry(sha1, pathname, mode);
	else if (S_ISDIR(mode))
		return READ_TREE_RECURSIVE;

	return 0;
}

static int basedir_len(const char *path)
{
	char *p = strrchr(path, '/');
	if (p)
		return p - path + 1;
	return 0;
}

void cgit_print_plain(struct cgit_context *ctx)
{
	const char *rev;
	unsigned char sha1[20];
	struct commit *commit;
	const char *paths[] = {NULL, NULL};
	char *slash = NULL;

	/* Take the first path component as the commit ID. */
	rev = ctx->qry.path;
	if (!rev || !rev[0]) {
		print_branches();
		return;
	}
	slash = strchr(ctx->qry.path, '/');
	if (slash)
		*slash = '\0';

	if (get_sha1(rev, sha1)) {
		if (slash)
			*slash = '/';
		not_found();
		return;
	}

	/* Parse out the actual path after the commit ID. */
	if (slash) {
		*slash = '/';
		paths[0] = slash + 1;
		match_baselen = basedir_len(paths[0]);
	}
	else {
		paths[0] = "";
		match_baselen = -1;
	}

	commit = lookup_commit_reference(sha1);
	if (!commit || parse_commit(commit)) {
		not_found();
		return;
	}

	if (match_baselen < 0)
		if (!print_dir(commit->tree->object.sha1, "", ""))
			return;
	read_tree_recursive(commit->tree, "", 0, 0, paths, walk_tree, NULL);
	if (!match)
		not_found();
	else if (match == 2)
		print_dir_tail();
}

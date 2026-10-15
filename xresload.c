/* xresload: put X resources (~/.Xresources) on the root window, like xrdb.
 *
 * Programs that use X resources (Xft.dpi, Xcursor.*, ...) read them from the
 * RESOURCE_MANAGER property of the first screen's root window. xrdb fills it,
 * but runs the C preprocessor on the file first and needs libXmu and libX11;
 * this talks XCB directly and does no preprocessing.
 *
 *   xresload [file]      merge file into the current resources (xrdb -merge);
 *                        default ~/.Xresources
 *   xresload -l file     replace the resources with file (xrdb -load)
 *   xresload -q          print the current resources (xrdb -query)
 *   xresload -r          remove all resources (xrdb -remove)
 *
 * File format: "name: value" lines; lines starting with "!" are comments, a
 * trailing backslash continues a line. The result is sorted by name, as xrdb does.
 */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>

#define MAXENT 4096

struct ent { const char *key, *val; };
static struct ent ents[MAXENT];
static int nents;

static void die(const char *msg)
{
	fprintf(stderr, "xresload: %s\n", msg);
	exit(1);
}

static void put(const char *key, const char *val)
{
	for (int i = 0; i < nents; i++)
		if (!strcmp(ents[i].key, key)) {
			ents[i].val = val;
			return;
		}
	if (nents == MAXENT) die("too many resources");
	ents[nents++] = (struct ent){ key, val };
}

/* Parse "name: value" lines in place (the text must stay alive). */
static void parse(char *s, const char *what)
{
	int warned = 0;
	while (*s) {
		char *line = s, *w = s;
		/* one logical line: join "\<newline>" continuations */
		for (; *s && *s != '\n'; s++) {
			if (*s == '\\' && s[1] == '\n') { s++; continue; }
			*w++ = *s;
		}
		if (*s) s++;
		*w = 0;
		while (*line == ' ' || *line == '\t') line++;
		if (!*line || *line == '!') continue;
		if (*line == '#') { /* xrdb's cpp would handle #include/#define; we don't run one */
			if (!warned++) fprintf(stderr, "xresload: %s: preprocessor lines are not supported, skipped\n", what);
			continue;
		}
		char *colon = strchr(line, ':');
		if (!colon) continue;
		char *ke = colon;
		while (ke > line && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
		*ke = 0;
		char *val = colon + 1;
		while (*val == ' ' || *val == '\t') val++;
		char *ve = val + strlen(val);
		while (ve > val && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r')) ve--;
		*ve = 0;
		if (*line) put(line, val);
	}
}

static char *slurp(const char *path)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return NULL;
	size_t cap = 4096, n = 0;
	char *b = malloc(cap);
	ssize_t r;
	while (b && (r = read(fd, b + n, cap - n - 1)) > 0)
		if ((n += (size_t)r) == cap - 1) b = realloc(b, cap *= 2);
	close(fd);
	if (b) b[n] = 0;
	return b;
}

static int cmp(const void *a, const void *b)
{
	return strcmp(((const struct ent *)a)->key, ((const struct ent *)b)->key);
}

static void check(xcb_connection_t *c, xcb_void_cookie_t ck)
{
	xcb_generic_error_t *e = xcb_request_check(c, ck);
	if (e) {
		fprintf(stderr, "xresload: X error %d\n", e->error_code);
		exit(1);
	}
}

int main(int argc, char **argv)
{
	char mode = 'm';
	const char *file = NULL;
	if (argc > 1 && argv[1][0] == '-' && argv[1][1] && !argv[1][2] && strchr("lqr", argv[1][1])) {
		mode = argv[1][1];
		file = argv[2];
		if ((mode == 'l') != (argc == 3) || argc > 3) mode = 0;
	} else if (argc <= 2 && !(argc == 2 && argv[1][0] == '-'))
		file = argv[1];
	else
		mode = 0;
	if (!mode) {
		fputs("usage: xresload [file] | -l file | -q | -r\n", stderr);
		return 2;
	}

	xcb_connection_t *c = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(c)) die("can't open display");
	/* RESOURCE_MANAGER lives on the first screen's root window */
	xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(c)).data->root;

	if (mode == 'r') {
		check(c, xcb_delete_property_checked(c, root, XCB_ATOM_RESOURCE_MANAGER));
		xcb_disconnect(c);
		return 0;
	}

	char *old = NULL;
	if (mode == 'q' || mode == 'm') {
		xcb_get_property_reply_t *r = xcb_get_property_reply(
			c, xcb_get_property(c, 0, root, XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 0, 1u << 28), NULL);
		if (r) {
			int len = xcb_get_property_value_length(r);
			if (mode == 'q') {
				if (len > 0) fwrite(xcb_get_property_value(r), 1, (size_t)len, stdout);
				free(r);
				xcb_disconnect(c);
				return 0;
			}
			old = malloc((size_t)len + 1);
			if (!old) die("out of memory");
			memcpy(old, xcb_get_property_value(r), (size_t)len);
			old[len] = 0;
			free(r);
		}
		if (mode == 'q') return 0;
		if (old) parse(old, "RESOURCE_MANAGER");
	}

	char def[4096];
	if (!file) {
		const char *home = getenv("HOME");
		if (!home) die("HOME is not set");
		snprintf(def, sizeof def, "%s/.Xresources", home);
		file = def;
	}
	char *text = slurp(file);
	if (!text) {
		fprintf(stderr, "xresload: can't read %s\n", file);
		return 1;
	}
	parse(text, file);
	qsort(ents, (size_t)nents, sizeof *ents, cmp);

	size_t total = 0;
	for (int i = 0; i < nents; i++) total += strlen(ents[i].key) + strlen(ents[i].val) + 3;
	char *buf = malloc(total + 1), *p = buf;
	if (!buf) die("out of memory");
	for (int i = 0; i < nents; i++) p += sprintf(p, "%s:\t%s\n", ents[i].key, ents[i].val);

	/* A request can't exceed the server's maximum length: write in chunks, the rest appended */
	size_t max = xcb_get_maximum_request_length(c) * 4 - 64, off = 0;
	uint8_t pm = XCB_PROP_MODE_REPLACE;
	do {
		size_t n = total - off < max ? total - off : max;
		check(c, xcb_change_property_checked(c, pm, root, XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 8, (uint32_t)n, buf + off));
		off += n;
		pm = XCB_PROP_MODE_APPEND;
	} while (off < total);
	xcb_disconnect(c);
	return 0;
}

/*
 * tacit-keyring-prompter: owns org.gnome.keyring.SystemPrompter and .PrivatePrompter (what /usr/lib/gcr-prompter
 * does) and draws nothing itself. Each gcr prompt becomes one ask on the DialogBridge (tacit-portal's
 * $XDG_RUNTIME_DIR/tacit-dialogs.sock, protocol in ../DIALOGS.md); the shell shows it in the notch.
 * gnome-keyring stays the store.
 *
 * Kinds: keyring.password (params.passwordNew = choose a new password) and keyring.confirm. See DIALOGS.md.
 * A cancelled or refused sheet ends the prompt the way a dismissed dialog does (no password, no error).
 * With no shell (or one that restarted, or whose sheet is not loaded yet) the prompt is retried for 30 seconds,
 * then ends as dismissed, so a caller is never left waiting on a missing shell.
 */
#define GCR_API_SUBJECT_TO_CHANGE
#include <gcr/gcr-base.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <glib-unix.h>
#include <json-glib/json-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NO_SHELL_WAIT_DEFAULT 30
#define IDLE_QUIT 60

static gboolean debug_on;
static int no_shell_wait = NO_SHELL_WAIT_DEFAULT; /* TACIT_KEYRING_NOSHELL_WAIT overrides, for tests */
#define DBG(...) G_STMT_START { if (debug_on) g_printerr(__VA_ARGS__); } G_STMT_END

/* ------------------------------------------------------------------ bridge client */

typedef void (*BridgeDone)(gboolean answered, JsonObject *result, const char *reason, gpointer ud);
typedef struct { BridgeDone cb; gpointer ud; } Pending;

static GSocketConnection *conn;
static GDataInputStream *in;
static GHashTable *pending; /* id -> Pending */
static guint64 next_id;
static GMainLoop *loop;
static gint64 last_active;

static char *sock_path(void)
{
	const char *o = g_getenv("TACIT_DIALOGS_SOCK");
	if (o && *o) return g_strdup(o);
	return g_build_filename(g_get_user_runtime_dir(), "tacit-dialogs.sock", NULL);
}

static void conn_drop(void)
{
	if (conn) {
		GHashTableIter it; gpointer k, v;
		GHashTable *old = pending;
		pending = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
		g_clear_object(&in);
		g_clear_object(&conn);
		g_hash_table_iter_init(&it, old);
		while (g_hash_table_iter_next(&it, &k, &v)) {
			Pending p = *(Pending *)v;
			p.cb(FALSE, NULL, "shell-gone", p.ud);
		}
		g_hash_table_destroy(old);
	}
}

static void bridge_write(JsonBuilder *b)
{
	JsonGenerator *g = json_generator_new();
	JsonNode *root = json_builder_get_root(b);
	json_generator_set_root(g, root);
	gsize len; char *s = json_generator_to_data(g, &len);
	char *line = g_strdup_printf("%s\n", s);
	GError *err = NULL;
	if (conn && !g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(conn)), line, strlen(line), NULL, NULL, &err)) {
		g_printerr("bridge write: %s\n", err->message);
		g_clear_error(&err);
	}
	/* the line may hold nothing secret (asks carry labels only), but zero it anyway */
	memset(line, 0, strlen(line));
	g_free(line); g_free(s);
	json_node_unref(root); g_object_unref(g); g_object_unref(b);
}

static void on_line(GObject *src, GAsyncResult *res, gpointer ud);

static gboolean conn_open(void)
{
	if (conn) return TRUE;
	char *path = sock_path();
	GSocketClient *cl = g_socket_client_new();
	g_socket_client_set_timeout(cl, 3);
	GSocketAddress *addr = g_unix_socket_address_new(path);
	GError *err = NULL;
	GSocketConnection *c = g_socket_client_connect(cl, G_SOCKET_CONNECTABLE(addr), NULL, &err);
	if (!c) {
		g_clear_error(&err);
		/* the portal is D-Bus activated: start it and try again */
		GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
		if (bus) {
			GVariant *r = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
				"StartServiceByName", g_variant_new("(su)", "org.freedesktop.impl.portal.desktop.tacit", 0), NULL,
				G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
			if (r) g_variant_unref(r);
		}
		for (int i = 0; i < 10 && !c; i++) {
			g_usleep(200000);
			c = g_socket_client_connect(cl, G_SOCKET_CONNECTABLE(addr), NULL, &err);
			g_clear_error(&err);
		}
	}
	g_object_unref(addr); g_object_unref(cl); g_free(path);
	if (!c) return FALSE;
	GSocket *sock = g_socket_connection_get_socket(c);
	g_socket_set_timeout(sock, 3);
	conn = c;
	JsonBuilder *b = json_builder_new();
	json_builder_begin_object(b);
	json_builder_set_member_name(b, "op"); json_builder_add_string_value(b, "hello");
	json_builder_set_member_name(b, "role"); json_builder_add_string_value(b, "client");
	json_builder_set_member_name(b, "proto"); json_builder_add_int_value(b, 1);
	json_builder_end_object(b);
	bridge_write(b);
	in = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(conn)));
	gsize n; char *l = g_data_input_stream_read_line(in, &n, NULL, &err);
	if (!l || !strstr(l, "\"hello\"")) {
		g_clear_error(&err); g_free(l);
		g_clear_object(&in); g_clear_object(&conn);
		return FALSE;
	}
	g_free(l);
	g_socket_set_timeout(sock, 0);
	g_data_input_stream_read_line_async(in, G_PRIORITY_DEFAULT, NULL, on_line, NULL);
	return TRUE;
}

static void on_line(GObject *src, GAsyncResult *res, gpointer ud)
{
	GError *err = NULL; gsize n;
	char *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(src), res, &n, &err);
	if (!line) { g_clear_error(&err); conn_drop(); return; }
	JsonParser *p = json_parser_new();
	if (json_parser_load_from_data(p, line, -1, NULL)) {
		JsonNode *root = json_parser_get_root(p);
		if (root && JSON_NODE_HOLDS_OBJECT(root)) {
			JsonObject *o = json_node_get_object(root);
			const char *op = json_object_get_string_member_with_default(o, "op", "");
			const char *id = json_object_get_string_member_with_default(o, "id", "");
			Pending *pd = g_hash_table_lookup(pending, id);
			if (pd && (!strcmp(op, "answer") || !strcmp(op, "reject"))) {
				Pending cp = *pd;
				g_hash_table_remove(pending, id);
				if (!strcmp(op, "answer")) {
					JsonObject *r = json_object_has_member(o, "result") ? json_object_get_object_member(o, "result") : NULL;
					cp.cb(TRUE, r, NULL, cp.ud);
				} else {
					cp.cb(FALSE, NULL, json_object_get_string_member_with_default(o, "reason", "refused"), cp.ud);
				}
			}
		}
	}
	/* the answer line holds the password: wipe it */
	memset(line, 0, n);
	g_free(line); g_object_unref(p);
	if (in) g_data_input_stream_read_line_async(in, G_PRIORITY_DEFAULT, NULL, on_line, NULL);
}

/* params is consumed. Returns the id (owned by the pending table until answered), or NULL when the
 * bridge cannot be reached (then cb is NOT called: the caller treats it as no-shell). */
static char *bridge_ask(const char *kind, JsonNode *params, BridgeDone cb, gpointer ud)
{
	if (!pending) pending = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	if (!conn_open()) { json_node_unref(params); return NULL; }
	char *id = g_strdup_printf("k%" G_GUINT64_FORMAT, ++next_id);
	Pending *pd = g_new0(Pending, 1); pd->cb = cb; pd->ud = ud;
	g_hash_table_insert(pending, g_strdup(id), pd);
	JsonBuilder *b = json_builder_new();
	json_builder_begin_object(b);
	json_builder_set_member_name(b, "op"); json_builder_add_string_value(b, "ask");
	json_builder_set_member_name(b, "id"); json_builder_add_string_value(b, id);
	json_builder_set_member_name(b, "kind"); json_builder_add_string_value(b, kind);
	json_builder_set_member_name(b, "params"); json_builder_add_value(b, params);
	json_builder_end_object(b);
	bridge_write(b);
	return id;
}

static void bridge_cancel(const char *id)
{
	if (!conn || !pending || !g_hash_table_remove(pending, id)) return;
	JsonBuilder *b = json_builder_new();
	json_builder_begin_object(b);
	json_builder_set_member_name(b, "op"); json_builder_add_string_value(b, "cancel");
	json_builder_set_member_name(b, "id"); json_builder_add_string_value(b, id);
	json_builder_end_object(b);
	bridge_write(b);
}

/* ------------------------------------------------------------------ the prompt */

#define TACIT_TYPE_PROMPT (tacit_prompt_get_type())
G_DECLARE_FINAL_TYPE(TacitPrompt, tacit_prompt, TACIT, PROMPT, GObject)

struct _TacitPrompt {
	GObject parent;
	char *title, *message, *description, *warning, *choice_label, *caller_window, *continue_label, *cancel_label;
	gboolean choice_chosen, password_new;
	gint strength;
	GTask *task;
	gboolean confirm;
	GCancellable *cancellable;
	gulong cancel_handler;
	char *ask_id;
	char *secret; /* secure memory */
	int asks;
	gint64 first_try;
	guint retry_src;
};

enum {
	P_TITLE = 1, P_MESSAGE, P_DESCRIPTION, P_WARNING, P_CHOICE_LABEL, P_CHOICE_CHOSEN, P_PASSWORD_NEW,
	P_PASSWORD_STRENGTH, P_CALLER_WINDOW, P_CONTINUE_LABEL, P_CANCEL_LABEL
};

static void tacit_prompt_iface_init(GcrPromptIface *iface);
G_DEFINE_TYPE_WITH_CODE(TacitPrompt, tacit_prompt, G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE(GCR_TYPE_PROMPT, tacit_prompt_iface_init))

static void tacit_prompt_init(TacitPrompt *self) { (void)self; }

static void set_str(char **slot, const GValue *v)
{
	g_free(*slot);
	*slot = g_value_dup_string(v);
}

static void tacit_prompt_set_property(GObject *obj, guint id, const GValue *v, GParamSpec *ps)
{
	TacitPrompt *s = TACIT_PROMPT(obj);
	switch (id) {
	case P_TITLE: set_str(&s->title, v); break;
	case P_MESSAGE: set_str(&s->message, v); break;
	case P_DESCRIPTION: set_str(&s->description, v); break;
	case P_WARNING: set_str(&s->warning, v); break;
	case P_CHOICE_LABEL: set_str(&s->choice_label, v); break;
	case P_CHOICE_CHOSEN: s->choice_chosen = g_value_get_boolean(v); break;
	case P_PASSWORD_NEW: s->password_new = g_value_get_boolean(v); break;
	case P_CALLER_WINDOW: set_str(&s->caller_window, v); break;
	case P_CONTINUE_LABEL: set_str(&s->continue_label, v); break;
	case P_CANCEL_LABEL: set_str(&s->cancel_label, v); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, ps);
	}
}

static void tacit_prompt_get_property(GObject *obj, guint id, GValue *v, GParamSpec *ps)
{
	TacitPrompt *s = TACIT_PROMPT(obj);
	switch (id) {
	case P_TITLE: g_value_set_string(v, s->title ? s->title : ""); break;
	case P_MESSAGE: g_value_set_string(v, s->message ? s->message : ""); break;
	case P_DESCRIPTION: g_value_set_string(v, s->description ? s->description : ""); break;
	case P_WARNING: g_value_set_string(v, s->warning ? s->warning : ""); break;
	case P_CHOICE_LABEL: g_value_set_string(v, s->choice_label ? s->choice_label : ""); break;
	case P_CHOICE_CHOSEN: g_value_set_boolean(v, s->choice_chosen); break;
	case P_PASSWORD_NEW: g_value_set_boolean(v, s->password_new); break;
	case P_PASSWORD_STRENGTH: g_value_set_int(v, s->strength); break;
	case P_CALLER_WINDOW: g_value_set_string(v, s->caller_window ? s->caller_window : ""); break;
	case P_CONTINUE_LABEL: g_value_set_string(v, s->continue_label ? s->continue_label : ""); break;
	case P_CANCEL_LABEL: g_value_set_string(v, s->cancel_label ? s->cancel_label : ""); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, ps);
	}
}

/* gnome-keyring reads password-strength after a new password and treats 0 as a blank one ("Store passwords
 * unencrypted?"), so a real password must never score 0. Same measure as gcr's own dialog, floored at 1. */
static gint strength_of(const char *pw)
{
	int length = strlen(pw), digit = 0, upper = 0, misc = 0;
	if (!length) return 0;
	for (const char *c = pw; *c; c++) {
		if (g_ascii_isdigit(*c)) digit++;
		else if (g_ascii_isupper(*c)) upper++;
		else if (!g_ascii_islower(*c)) misc++;
	}
	length = MIN(length, 5); digit = MIN(digit, 3); upper = MIN(upper, 3); misc = MIN(misc, 3);
	return CLAMP(length * 10 - 20 + digit * 10 + misc * 15 + upper * 10, 1, 100);
}

static void drop_secret(TacitPrompt *s)
{
	if (s->secret) { gcr_secure_memory_strfree(s->secret); s->secret = NULL; }
}

static void ask_cleanup(TacitPrompt *s)
{
	if (s->cancel_handler && s->cancellable) g_signal_handler_disconnect(s->cancellable, s->cancel_handler);
	s->cancel_handler = 0;
	g_clear_object(&s->cancellable);
	if (s->retry_src) { g_source_remove(s->retry_src); s->retry_src = 0; }
	g_clear_pointer(&s->ask_id, g_free);
}

static void tacit_prompt_finalize(GObject *obj)
{
	TacitPrompt *s = TACIT_PROMPT(obj);
	if (s->ask_id) bridge_cancel(s->ask_id);
	ask_cleanup(s);
	drop_secret(s);
	g_clear_object(&s->task);
	g_free(s->title); g_free(s->message); g_free(s->description); g_free(s->warning); g_free(s->choice_label);
	g_free(s->caller_window); g_free(s->continue_label); g_free(s->cancel_label);
	G_OBJECT_CLASS(tacit_prompt_parent_class)->finalize(obj);
}

static void tacit_prompt_class_init(TacitPromptClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS(klass);
	oc->get_property = tacit_prompt_get_property;
	oc->set_property = tacit_prompt_set_property;
	oc->finalize = tacit_prompt_finalize;
	g_object_class_override_property(oc, P_TITLE, "title");
	g_object_class_override_property(oc, P_MESSAGE, "message");
	g_object_class_override_property(oc, P_DESCRIPTION, "description");
	g_object_class_override_property(oc, P_WARNING, "warning");
	g_object_class_override_property(oc, P_CHOICE_LABEL, "choice-label");
	g_object_class_override_property(oc, P_CHOICE_CHOSEN, "choice-chosen");
	g_object_class_override_property(oc, P_PASSWORD_NEW, "password-new");
	g_object_class_override_property(oc, P_PASSWORD_STRENGTH, "password-strength");
	g_object_class_override_property(oc, P_CALLER_WINDOW, "caller-window");
	g_object_class_override_property(oc, P_CONTINUE_LABEL, "continue-label");
	g_object_class_override_property(oc, P_CANCEL_LABEL, "cancel-label");
}

static void add_str(JsonBuilder *b, const char *name, const char *v)
{
	json_builder_set_member_name(b, name);
	json_builder_add_string_value(b, v ? v : "");
}

static gboolean send_ask(gpointer ud);

/* finish the pending task: status 1 = continue, 0 = cancelled/dismissed */
static void finish(TacitPrompt *s, int status)
{
	GTask *t = s->task;
	s->task = NULL;
	ask_cleanup(s);
	last_active = g_get_monotonic_time();
	if (t) { g_task_return_int(t, status); g_object_unref(t); }
}

static void on_answer(gboolean answered, JsonObject *result, const char *reason, gpointer ud)
{
	TacitPrompt *s = ud;
	g_clear_pointer(&s->ask_id, g_free);
	if (!s->task) return;
	/* no shell yet, a shell that restarted under the ask, or one whose sheet has not loaded yet (startup) */
	if (!answered && reason && (!strcmp(reason, "no-shell") || !strcmp(reason, "shell-gone") || !strcmp(reason, "unsupported"))) {
		if (g_get_monotonic_time() - s->first_try < (gint64)no_shell_wait * G_USEC_PER_SEC) {
			s->retry_src = g_timeout_add_seconds(2, send_ask, s);
			return;
		}
	}
	if (!answered) { DBG("prompt ended: %s\n", reason ? reason : "?"); finish(s, 0); return; }
	const char *reply = result ? json_object_get_string_member_with_default(result, "reply", "continue") : "continue";
	if (strcmp(reply, "continue")) { finish(s, 0); return; }
	if (result && json_object_has_member(result, "choice")) {
		gboolean c = json_object_get_boolean_member_with_default(result, "choice", FALSE);
		if (c != s->choice_chosen) { s->choice_chosen = c; g_object_notify(G_OBJECT(s), "choice-chosen"); }
	}
	if (!s->confirm) {
		const char *pw = result ? json_object_get_string_member_with_default(result, "password", NULL) : NULL;
		if (!pw) { finish(s, 0); return; }
		drop_secret(s);
		s->secret = gcr_secure_memory_strdup(pw);
		if (s->password_new) {
			s->strength = strength_of(pw);
			g_object_notify(G_OBJECT(s), "password-strength");
		}
		DBG("password answer, %zu bytes, new=%d strength=%d\n", strlen(pw), s->password_new, s->strength);
	}
	finish(s, 1);
}

static gboolean send_ask(gpointer ud)
{
	TacitPrompt *s = ud;
	s->retry_src = 0;
	if (!s->task) return G_SOURCE_REMOVE;
	JsonBuilder *b = json_builder_new();
	json_builder_begin_object(b);
	json_builder_set_member_name(b, "mode"); json_builder_add_string_value(b, s->confirm ? "confirm" : "password");
	add_str(b, "title", s->title);
	add_str(b, "message", s->message);
	add_str(b, "description", s->description);
	add_str(b, "warning", s->warning);
	add_str(b, "choiceLabel", s->choice_label);
	json_builder_set_member_name(b, "choiceChosen"); json_builder_add_boolean_value(b, s->choice_chosen);
	json_builder_set_member_name(b, "passwordNew"); json_builder_add_boolean_value(b, s->password_new);
	add_str(b, "continueLabel", s->continue_label);
	add_str(b, "cancelLabel", s->cancel_label);
	/* the daemon asks again on the same prompt with `warning` set after a wrong password */
	json_builder_set_member_name(b, "retry"); json_builder_add_boolean_value(b, s->asks > 0 && s->warning && *s->warning);
	json_builder_end_object(b);
	JsonNode *params = json_builder_get_root(b);
	g_object_unref(b);
	s->ask_id = bridge_ask(s->confirm ? "keyring.confirm" : "keyring.password", params, on_answer, s);
	if (!s->ask_id) {
		if (g_get_monotonic_time() - s->first_try < (gint64)no_shell_wait * G_USEC_PER_SEC)
			s->retry_src = g_timeout_add_seconds(2, send_ask, s);
		else
			finish(s, 0);
	}
	return G_SOURCE_REMOVE;
}

static void on_cancelled(GCancellable *c, gpointer ud)
{
	TacitPrompt *s = ud;
	if (s->ask_id) { bridge_cancel(s->ask_id); }
	GTask *t = s->task;
	s->task = NULL;
	ask_cleanup(s);
	if (t) { g_task_return_int(t, 0); g_object_unref(t); }
}

static void start(TacitPrompt *s, gboolean confirm, GCancellable *c, GAsyncReadyCallback cb, gpointer ud)
{
	if (s->task) { /* a second call while one is open: the first is superseded */
		if (s->ask_id) bridge_cancel(s->ask_id);
		finish(s, 0);
	}
	s->confirm = confirm;
	drop_secret(s);
	s->task = g_task_new(s, c, cb, ud);
	if (c) {
		s->cancellable = g_object_ref(c);
		s->cancel_handler = g_signal_connect(c, "cancelled", G_CALLBACK(on_cancelled), s);
	}
	s->first_try = g_get_monotonic_time();
	last_active = s->first_try;
	/* from the idle loop: if c is already cancelled the handler above has fired */
	if (!g_task_get_completed(s->task)) { send_ask(s); s->asks++; }
}

static void p_password_async(GcrPrompt *p, GCancellable *c, GAsyncReadyCallback cb, gpointer ud)
{
	start(TACIT_PROMPT(p), FALSE, c, cb, ud);
}

static const gchar *p_password_finish(GcrPrompt *p, GAsyncResult *r, GError **err)
{
	TacitPrompt *s = TACIT_PROMPT(p);
	gssize st = g_task_propagate_int(G_TASK(r), err);
	DBG("password_finish status %zd, secret %s\n", st, s->secret ? "set" : "none");
	return st == 1 ? s->secret : NULL;
}

static void p_confirm_async(GcrPrompt *p, GCancellable *c, GAsyncReadyCallback cb, gpointer ud)
{
	start(TACIT_PROMPT(p), TRUE, c, cb, ud);
}

static GcrPromptReply p_confirm_finish(GcrPrompt *p, GAsyncResult *r, GError **err)
{
	(void)p;
	return g_task_propagate_int(G_TASK(r), err) == 1 ? GCR_PROMPT_REPLY_CONTINUE : GCR_PROMPT_REPLY_CANCEL;
}

static void p_close(GcrPrompt *p)
{
	TacitPrompt *s = TACIT_PROMPT(p);
	if (s->ask_id) bridge_cancel(s->ask_id);
	if (s->task) finish(s, 0);
	else ask_cleanup(s);
	drop_secret(s);
}

static void tacit_prompt_iface_init(GcrPromptIface *iface)
{
	iface->prompt_password_async = p_password_async;
	iface->prompt_password_finish = p_password_finish;
	iface->prompt_confirm_async = p_confirm_async;
	iface->prompt_confirm_finish = p_confirm_finish;
	iface->prompt_close = p_close;
}

/* ------------------------------------------------------------------ bus names */

static GcrSystemPrompter *prompter;
static gboolean registered;

static void on_bus_acquired(GDBusConnection *c, const gchar *name, gpointer ud)
{
	(void)name; (void)ud;
	if (registered) return;
	registered = TRUE;
	gcr_system_prompter_register(prompter, c);
}

static void on_name_acquired(GDBusConnection *c, const gchar *name, gpointer ud)
{
	(void)c; (void)ud;
	DBG("owns %s\n", name);
}

static void on_name_lost(GDBusConnection *c, const gchar *name, gpointer ud)
{
	(void)c; (void)ud;
	g_printerr("lost %s\n", name);
	g_main_loop_quit(loop);
}

static gboolean idle_check(gpointer ud)
{
	(void)ud;
	if (!gcr_system_prompter_get_prompting(prompter) &&
	    g_get_monotonic_time() - last_active > (gint64)IDLE_QUIT * G_USEC_PER_SEC)
		g_main_loop_quit(loop);
	return G_SOURCE_CONTINUE;
}

static gboolean on_signal(gpointer ud) { (void)ud; g_main_loop_quit(loop); return G_SOURCE_REMOVE; }

int main(int argc, char **argv)
{
	debug_on = g_getenv("TACIT_KEYRING_DEBUG") != NULL;
	if (g_getenv("TACIT_KEYRING_NOSHELL_WAIT")) no_shell_wait = atoi(g_getenv("TACIT_KEYRING_NOSHELL_WAIT"));
	for (int i = 1; i < argc; i++)
		if (!strcmp(argv[i], "--no-idle-quit")) last_active = G_MAXINT64 / 2;
	if (!last_active) last_active = g_get_monotonic_time();
	loop = g_main_loop_new(NULL, FALSE);
	prompter = gcr_system_prompter_new(GCR_SYSTEM_PROMPTER_SINGLE, TACIT_TYPE_PROMPT);
	const char *names[] = { "org.gnome.keyring.SystemPrompter", "org.gnome.keyring.PrivatePrompter" };
	guint owners[2];
	for (int i = 0; i < 2; i++)
		owners[i] = g_bus_own_name(G_BUS_TYPE_SESSION, names[i], G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT,
			on_bus_acquired, on_name_acquired, on_name_lost, NULL, NULL);
	g_timeout_add_seconds(5, idle_check, NULL);
	g_unix_signal_add(SIGTERM, on_signal, NULL);
	g_unix_signal_add(SIGINT, on_signal, NULL);
	g_main_loop_run(loop);
	gcr_system_prompter_unregister(prompter, TRUE);
	for (int i = 0; i < 2; i++) g_bus_unown_name(owners[i]);
	g_object_unref(prompter);
	return 0;
}

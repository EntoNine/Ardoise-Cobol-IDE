/*
 * cwse_ide.c — GTK4 Hybrid IDE for CWSE (ncurses COBOL editor)
 *
 * Compile:
 *   gcc cwse_ide.c cwse.c -o cwse_studio \
 *       $(pkg-config --cflags --libs gtk4 vte-2.91-gtk4) \
 *       -lncurses -Wall -Wextra -O2
 */

#include <gtk/gtk.h>
#include <vte/vte.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── Configuration ───────────────────────────── */
#define APP_ID        "org.cwse.ide"
#define SIDEBAR_WIDTH  230
#define FONT_TERMINAL "Monospace 12"

/* Déclaration du moteur ncurses (cwse.c) */
int run_cwse_engine(int argc, char **argv);

/* ── Colonnes GtkListStore ───────────────────── */
enum { COL_ICON, COL_NAME, COL_FULLPATH, COL_IS_DIR, N_COLS };

/* ── État global de l'application ────────────── */
typedef struct {
    GtkApplication *gapp;
    GtkWidget      *window;
    GtkWidget      *path_label;
    GtkWidget      *tree_view;
    GtkListStore   *store;
    GtkWidget      *terminal;
    GtkWidget      *status_label;
    GtkWidget      *cwse_status_label;
    char            cwd[PATH_MAX];
    char            active_file[PATH_MAX];
} App;

static App G = {0};

/* ── Palette Catppuccin Mocha ─────────────────── */
static const GdkRGBA PALETTE[16] = {
    {0.094, 0.094, 0.133, 1.0},
    {0.949, 0.357, 0.443, 1.0},
    {0.651, 0.890, 0.631, 1.0},
    {0.973, 0.784, 0.455, 1.0},
    {0.537, 0.706, 0.980, 1.0},
    {0.796, 0.651, 0.969, 1.0},
    {0.529, 0.886, 0.878, 1.0},
    {0.804, 0.839, 0.957, 1.0},
    {0.365, 0.369, 0.451, 1.0},
    {0.949, 0.357, 0.443, 1.0},
    {0.651, 0.890, 0.631, 1.0},
    {0.973, 0.784, 0.455, 1.0},
    {0.537, 0.706, 0.980, 1.0},
    {0.796, 0.651, 0.969, 1.0},
    {0.529, 0.886, 0.878, 1.0},
    {0.804, 0.839, 0.957, 1.0},
};
static const GdkRGBA COL_FG = {0.804, 0.839, 0.957, 1.0};
static const GdkRGBA COL_BG = {0.118, 0.118, 0.169, 1.0};

/* ── CSS ─────────────────────────────────────── */
static const char APP_CSS[] =
    "window { background: #1e1e1e; }"
    "headerbar { background: #252525; border-bottom: 1px solid #333; color: #d4d4d4; padding: 0 8px; }"
    "headerbar .title { color: #ccc; font-weight: 500; font-size: 13px; }"
    "button.toolbar-btn { background: transparent; color: #969696; border: none; border-radius: 3px; padding: 6px 12px; }"
    "button.toolbar-btn:hover { background: #3c3c3c; color: #fff; }"
    "button.toolbar-btn:active { background: #323232; }"
    "button.run-btn { background: #4a4a4a; color: #fff; font-weight: 500; border: 1px solid #555; border-radius: 3px; padding: 5px 12px; }"
    "button.run-btn:hover { background: #5a5a5a; }"
    ".sidebar { background: #252526; border-right: 1px solid #333; }"
    ".path-bar { background: #1a1a1a; padding: 4px 10px; border-bottom: 1px solid #333; }"
    ".path-bar label { color: #aaa; font-family: monospace; font-size: 11px; }"
    ".section-label { color: #666; font-size: 10px; font-weight: 700; padding: 8px 10px 4px; }"
    "treeview { background: #252526; color: #ccc; font-family: monospace; font-size: 12px; }"
    "treeview:selected { background: #37373d; color: #fff; }"
    "treeview header button { background: #1e1e1e; color: #888; border: none; border-bottom: 1px solid #333; }"
    "paned > separator { background: #333; min-width: 1px; }"
    ".status-bar { background: #007acc; padding: 3px 10px; }"
    ".status-bar label { color: #fff; font-size: 11px; }"
    ".active-badge { background: #3c3c3c; color: #ccc; font-family: monospace; font-size: 11px; border-radius: 2px; padding: 1px 6px; border: 1px solid #454545; }";

/* ── Utilitaires ─────────────────────────────── */
static int cmp_name(gconstpointer a, gconstpointer b) {
    return g_ascii_strcasecmp(*(const char **)a, *(const char **)b);
}

static const char *file_icon(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return "text-x-generic-symbolic";
    if (!g_ascii_strcasecmp(ext, ".cbl") ||
        !g_ascii_strcasecmp(ext, ".cob") ||
        !g_ascii_strcasecmp(ext, ".cpy"))
        return "text-x-script-symbolic";
    if (!g_ascii_strcasecmp(ext, ".c") ||
        !g_ascii_strcasecmp(ext, ".h"))
        return "text-x-csrc-symbolic";
    return "text-x-generic-symbolic";
}

static void set_status(const char *msg) {
    gtk_label_set_text(GTK_LABEL(G.status_label), msg);
}

/* ── Spawn du moteur CWSE dans le VTE ────────── */
static void spawn_cwse(const char *filepath) {
    vte_terminal_reset(VTE_TERMINAL(G.terminal), TRUE, TRUE);

    if (filepath) {
        g_strlcpy(G.active_file, filepath, PATH_MAX);
        char label[PATH_MAX + 8];
        g_snprintf(label, sizeof label, " ✎  %s", filepath);
        gtk_label_set_text(GTK_LABEL(G.cwse_status_label), label);
        set_status(" CWSE opened");
    } else {
        G.active_file[0] = '\0';
        gtk_label_set_text(GTK_LABEL(G.cwse_status_label), "  (no file)");
        set_status(" CWSE started; double-click on a file to edit");
    }

    const char *argv_spawn[4] = {
        "/proc/self/exe",
        "--embed-engine",
        filepath,   /* peut être NULL */
        NULL
    };

    vte_terminal_spawn_async(
        VTE_TERMINAL(G.terminal),
        VTE_PTY_DEFAULT,
        G.cwd,
        (char **)argv_spawn,
        NULL,
        G_SPAWN_SEARCH_PATH,
        NULL, NULL, NULL,
        -1,
        NULL, NULL, NULL
    );

    gtk_widget_grab_focus(G.terminal);
}

/* ── Remplissage de l'arbre de fichiers ──────── */
static void populate_tree(const char *dirpath) {
    g_strlcpy(G.cwd, dirpath, PATH_MAX);
    gtk_label_set_text(GTK_LABEL(G.path_label), dirpath);
    gtk_list_store_clear(G.store);

    DIR *d = opendir(dirpath);
    if (!d) {
        set_status(" Unable to open the folder");
        return;
    }

    GPtrArray *dirs  = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);

    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.' && strcmp(ent->d_name, "..") != 0) continue;
        if (strcmp(ent->d_name, ".") == 0) continue;

        char full[PATH_MAX];
        g_snprintf(full, sizeof full, "%s/%s", dirpath, ent->d_name);

        struct stat st;
        if (stat(full, &st) != 0) continue;

        g_ptr_array_add(S_ISDIR(st.st_mode) ? dirs : files,
                        g_strdup(ent->d_name));
    }
    closedir(d);

    g_ptr_array_sort(dirs,  cmp_name);
    g_ptr_array_sort(files, cmp_name);

    GtkTreeIter iter;

    for (guint i = 0; i < dirs->len; i++) {
        const char *n  = dirs->pdata[i];
        char fp[PATH_MAX];
        g_snprintf(fp, sizeof fp, "%s/%s", dirpath, n);
        gtk_list_store_append(G.store, &iter);
        gtk_list_store_set(G.store, &iter,
            COL_ICON,     strcmp(n, "..") == 0 ? "go-up-symbolic" : "folder-symbolic",
            COL_NAME,     n,
            COL_FULLPATH, fp,
            COL_IS_DIR,   TRUE,
            -1);
    }

    for (guint i = 0; i < files->len; i++) {
        const char *n = files->pdata[i];
        char fp[PATH_MAX];
        g_snprintf(fp, sizeof fp, "%s/%s", dirpath, n);
        gtk_list_store_append(G.store, &iter);
        gtk_list_store_set(G.store, &iter,
            COL_ICON,     file_icon(n),
            COL_NAME,     n,
            COL_FULLPATH, fp,
            COL_IS_DIR,   FALSE,
            -1);
    }

    g_ptr_array_free(dirs,  TRUE);
    g_ptr_array_free(files, TRUE);
}

/* ── Signaux ─────────────────────────────────── */
static void on_row_activated(GtkTreeView *tv, GtkTreePath *tp,
                              GtkTreeViewColumn *col, gpointer ud)
{
    (void)col; (void)ud;
    GtkTreeModel *model = gtk_tree_view_get_model(tv);
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(model, &iter, tp)) return;

    char     *fullpath = NULL;
    gboolean  is_dir   = FALSE;
    gtk_tree_model_get(model, &iter,
        COL_FULLPATH, &fullpath,
        COL_IS_DIR,   &is_dir, -1);

    if (!fullpath) return;
    if (is_dir) populate_tree(fullpath);
    else        spawn_cwse(fullpath);
    g_free(fullpath);
}

static void on_child_exited(VteTerminal *term, int status, gpointer ud) {
    (void)term; (void)status; (void)ud;
    G.active_file[0] = '\0';
    gtk_label_set_text(GTK_LABEL(G.cwse_status_label), "  (no file)");
    set_status(" CWSE finished; double-click to restart");
}

/* ── Dialogue : Nouveau fichier ──────────────── */
typedef struct { GtkWidget *dlg; GtkWidget *entry; } NewFileDlg;

static void on_new_file_response(GtkDialog *dlg, int resp, gpointer ud) {
    NewFileDlg *nfd = ud;
    if (resp == GTK_RESPONSE_ACCEPT) {
        const char *name = gtk_editable_get_text(GTK_EDITABLE(nfd->entry));
        if (name && *name) {
            char fp[PATH_MAX];
            g_snprintf(fp, sizeof fp, "%s/%s", G.cwd, name);
            FILE *f = fopen(fp, "a");
            if (f) fclose(f);
            populate_tree(G.cwd);
            spawn_cwse(fp);
        }
    }
    gtk_window_destroy(GTK_WINDOW(nfd->dlg));
    g_free(nfd);
}

static void on_new_file_clicked(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;

    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        "New COBOL File", GTK_WINDOW(G.window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Create",   GTK_RESPONSE_ACCEPT,
        NULL);

    GtkWidget *ca  = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 18); gtk_widget_set_margin_end(box, 18);
    gtk_widget_set_margin_top(box, 14);   gtk_widget_set_margin_bottom(box, 10);

    GtkWidget *lbl = gtk_label_new("File name:");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);

    GtkWidget *entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), "program.cbl");
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);

    gtk_box_append(GTK_BOX(box), lbl);
    gtk_box_append(GTK_BOX(box), entry);
    gtk_box_append(GTK_BOX(ca), box);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);

    NewFileDlg *nfd = g_new(NewFileDlg, 1);
    nfd->dlg   = dlg;
    nfd->entry = entry;
    g_signal_connect(dlg, "response", G_CALLBACK(on_new_file_response), nfd);
    gtk_widget_show(dlg);
}

/* ── Dialogue : Ouvrir dossier ───────────────── */
static void on_folder_dlg_response(GtkDialog *dlg, int resp, gpointer ud) {
    (void)ud;
    if (resp == GTK_RESPONSE_ACCEPT) {
        GFile *f = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dlg));
        if (f) {
            char *path = g_file_get_path(f);
            if (path) { populate_tree(path); g_free(path); }
            g_object_unref(f);
        }
    }
    gtk_window_destroy(GTK_WINDOW(dlg));
}

static void on_open_folder_clicked(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Open Folder", GTK_WINDOW(G.window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open",  GTK_RESPONSE_ACCEPT,
        NULL);
    GFile *cur = g_file_new_for_path(G.cwd);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), cur, NULL);
    g_object_unref(cur);
    g_signal_connect(dlg, "response", G_CALLBACK(on_folder_dlg_response), NULL);
    gtk_widget_show(dlg);
}

static void on_refresh_clicked(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;
    populate_tree(G.cwd);
}

static void on_launch_cwse_clicked(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;
    spawn_cwse(NULL);
}

/* ── Dialogue générique (Credits / Terms) ────── */
static void show_info_dialog(const char *title_text, const char *body_text) {
    GtkWidget *dlg = gtk_dialog_new();
    gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(G.window));
    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
    gtk_window_set_decorated(GTK_WINDOW(dlg), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 360, -1);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 20);    gtk_widget_set_margin_bottom(box, 20);
    gtk_widget_set_margin_start(box, 24);  gtk_widget_set_margin_end(box, 24);

    GtkWidget *title = gtk_label_new(title_text);
    gtk_widget_add_css_class(title, "title-2");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

    GtkWidget *label = gtk_label_new(body_text);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);

    GtkWidget *btn = gtk_button_new_with_label("Close");
    gtk_widget_set_halign(btn, GTK_ALIGN_END);
    g_signal_connect_swapped(btn, "clicked", G_CALLBACK(gtk_window_destroy), dlg);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_box_append(GTK_BOX(box), title);
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), btn);
    gtk_box_append(GTK_BOX(content), box);
    gtk_window_present(GTK_WINDOW(dlg));
}

static void on_show_credits(GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a; (void)p; (void)ud;
    show_info_dialog("CWSE IDE",
        "Ardoise COBOL IDE\n\nCreated by Ento9");
}

static void on_show_terms(GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a; (void)p; (void)ud;
    show_info_dialog("Usage Conditions",
        "Ardoise COBOL IDE is distributed under the MIT license.");
}

/* ── Sidebar ─────────────────────────────────── */
static GtkWidget *build_sidebar(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(box, "sidebar");
    gtk_widget_set_size_request(box, SIDEBAR_WIDTH, -1);

    /* Barre de chemin */
    GtkWidget *path_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(path_bar, "path-bar");
    G.path_label = gtk_label_new(G.cwd);
    gtk_label_set_ellipsize(GTK_LABEL(G.path_label), PANGO_ELLIPSIZE_START);
    gtk_label_set_xalign(GTK_LABEL(G.path_label), 0.0f);
    gtk_widget_set_hexpand(G.path_label, TRUE);
    gtk_box_append(GTK_BOX(path_bar), G.path_label);
    gtk_box_append(GTK_BOX(box), path_bar);

    /* Étiquette de section */
    GtkWidget *sec = gtk_label_new("FILES");
    gtk_widget_add_css_class(sec, "section-label");
    gtk_label_set_xalign(GTK_LABEL(sec), 0.0f);
    gtk_box_append(GTK_BOX(box), sec);

    /* ListStore + TreeView */
    G.store = gtk_list_store_new(N_COLS,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);

    G.tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(G.store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(G.tree_view), FALSE);
    gtk_tree_view_set_activate_on_single_click(GTK_TREE_VIEW(G.tree_view), FALSE);

    GtkCellRenderer   *icon_r = gtk_cell_renderer_pixbuf_new();
    g_object_set(icon_r, "stock-size", GTK_ICON_SIZE_NORMAL, NULL);
    GtkTreeViewColumn *icon_c = gtk_tree_view_column_new_with_attributes(
        NULL, icon_r, "icon-name", COL_ICON, NULL);
    gtk_tree_view_column_set_min_width(icon_c, 26);
    gtk_tree_view_column_set_sizing(icon_c, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_append_column(GTK_TREE_VIEW(G.tree_view), icon_c);

    GtkCellRenderer   *name_r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *name_c = gtk_tree_view_column_new_with_attributes(
        NULL, name_r, "text", COL_NAME, NULL);
    gtk_tree_view_column_set_expand(name_c, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(G.tree_view), name_c);

    g_signal_connect(G.tree_view, "row-activated",
                     G_CALLBACK(on_row_activated), NULL);

    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), G.tree_view);
    gtk_widget_set_vexpand(sw, TRUE);
    gtk_box_append(GTK_BOX(box), sw);

    return box;
}

/* ── Panneau terminal ────────────────────────── */
static GtkWidget *build_terminal_pane(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    G.terminal = vte_terminal_new();

    vte_terminal_set_colors(VTE_TERMINAL(G.terminal),
        &COL_FG, &COL_BG, PALETTE, G_N_ELEMENTS(PALETTE));

    static const GdkRGBA cursor_bg = {1.0, 0.65, 0.0, 1.0};
    static const GdkRGBA cursor_fg = {0.0, 0.0, 0.0, 1.0};
    vte_terminal_set_color_cursor(VTE_TERMINAL(G.terminal), &cursor_bg);
    vte_terminal_set_color_cursor_foreground(VTE_TERMINAL(G.terminal), &cursor_fg);
    vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(G.terminal), VTE_CURSOR_BLINK_OFF);
    vte_terminal_set_cursor_shape(VTE_TERMINAL(G.terminal), VTE_CURSOR_SHAPE_BLOCK);

    PangoFontDescription *font = pango_font_description_from_string(FONT_TERMINAL);
    vte_terminal_set_font(VTE_TERMINAL(G.terminal), font);
    pango_font_description_free(font);

    vte_terminal_set_scrollback_lines(VTE_TERMINAL(G.terminal), 10000);
    vte_terminal_set_mouse_autohide(VTE_TERMINAL(G.terminal), TRUE);
    vte_terminal_set_rewrap_on_resize(VTE_TERMINAL(G.terminal), TRUE);

    g_signal_connect(G.terminal, "child-exited",
                     G_CALLBACK(on_child_exited), NULL);

    gtk_widget_set_vexpand(G.terminal, TRUE);
    gtk_widget_set_hexpand(G.terminal, TRUE);
    gtk_box_append(GTK_BOX(box), G.terminal);

    /* Barre de statut */
    GtkWidget *status_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(status_row, "status-bar");

    G.status_label = gtk_label_new(" READY");
    gtk_label_set_xalign(GTK_LABEL(G.status_label), 0.0f);
    gtk_widget_set_hexpand(G.status_label, TRUE);
    gtk_box_append(GTK_BOX(status_row), G.status_label);

    G.cwse_status_label = gtk_label_new("  (no file)");
    gtk_widget_add_css_class(G.cwse_status_label, "active-badge");
    gtk_box_append(GTK_BOX(status_row), G.cwse_status_label);

    gtk_box_append(GTK_BOX(status_row), gtk_label_new(" "));
    gtk_box_append(GTK_BOX(box), status_row);

    return box;
}

/* ── Barre d'en-tête ─────────────────────────── */
static GtkWidget *build_headerbar(void) {
    GtkWidget *hb = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hb), TRUE);

    GtkWidget *title_lbl = gtk_label_new("Ardoise COBOL IDE");
    gtk_widget_add_css_class(title_lbl, "title");
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(hb), title_lbl);

    /* Gauche : New / Open / Refresh */
    GtkWidget *lbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    static const struct { const char *label; const char *tooltip; GCallback cb; } left_btns[] = {
        { "New",     "New COBOL File", G_CALLBACK(on_new_file_clicked)   },
        { "Open",    "Open Folder",     G_CALLBACK(on_open_folder_clicked) },
        { "Refresh", "Refresh Tree",    G_CALLBACK(on_refresh_clicked)    },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(left_btns); i++) {
        GtkWidget *b = gtk_button_new_with_label(left_btns[i].label);
        gtk_widget_add_css_class(b, "toolbar-btn");
        gtk_widget_set_tooltip_text(b, left_btns[i].tooltip);
        g_signal_connect(b, "clicked", left_btns[i].cb, NULL);
        gtk_box_append(GTK_BOX(lbox), b);
    }
    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), lbox);

    /* Droite : Launch + Menu */
    GtkWidget *run_btn = gtk_button_new_with_label("▶ Launch CWSE");
    gtk_widget_add_css_class(run_btn, "run-btn");
    gtk_widget_set_tooltip_text(run_btn, "Launch CWSE without a file");
    g_signal_connect(run_btn, "clicked", G_CALLBACK(on_launch_cwse_clicked), NULL);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), run_btn);

    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Credits",               "app.credits");
    g_menu_append(menu, "Usage Conditions", "app.terms");

    GtkWidget *menu_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_btn), G_MENU_MODEL(menu));
    gtk_widget_add_css_class(menu_btn, "toolbar-btn");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), menu_btn);
    g_object_unref(menu);

    return hb;
}

/* ── Activation ──────────────────────────────── */
static void on_activate(GApplication *gapp, gpointer ud) {
    (void)ud;

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, APP_CSS);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    G.window = gtk_application_window_new(GTK_APPLICATION(gapp));
    gtk_window_set_title(GTK_WINDOW(G.window), "Ardoise COBOL IDE");
    gtk_window_set_default_size(GTK_WINDOW(G.window), 1280, 800);
    gtk_window_set_titlebar(GTK_WINDOW(G.window), build_headerbar());

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(paned), SIDEBAR_WIDTH);
    g_object_set(paned,
        "resize-start-child", FALSE,
        "resize-end-child",   TRUE,
        "shrink-start-child", FALSE,
        "shrink-end-child",   FALSE,
        NULL);
    gtk_paned_set_start_child(GTK_PANED(paned), build_sidebar());
    gtk_paned_set_end_child(GTK_PANED(paned),   build_terminal_pane());
    gtk_window_set_child(GTK_WINDOW(G.window), paned);

    if (G.cwd[0] == '\0' && !getcwd(G.cwd, PATH_MAX - 1))
        g_strlcpy(G.cwd, ".", PATH_MAX);

    populate_tree(G.cwd);
    spawn_cwse(NULL);

    /* Actions du menu */
    static const struct { const char *name; GCallback cb; } actions[] = {
        { "credits", G_CALLBACK(on_show_credits) },
        { "terms",   G_CALLBACK(on_show_terms)   },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(actions); i++) {
        GSimpleAction *a = g_simple_action_new(actions[i].name, NULL);
        g_signal_connect(a, "activate", actions[i].cb, NULL);
        g_action_map_add_action(G_ACTION_MAP(G.gapp), G_ACTION(a));
        g_object_unref(a);
    }

    gtk_window_present(GTK_WINDOW(G.window));
}

/* ── Point d'entrée ──────────────────────────── */
int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--embed-engine") == 0)
        return run_cwse_engine(argc - 1, &argv[1]);

    G.gapp = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(G.gapp, "activate", G_CALLBACK(on_activate), NULL);

    int rc = g_application_run(G_APPLICATION(G.gapp), argc, argv);
    g_object_unref(G.gapp);
    return rc;
}
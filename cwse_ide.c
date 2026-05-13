/*
 * cwse_ide.c — GTK4 Hybrid IDE for CWSE (ncurses COBOL editor)
 *
 * Compile:
 *   gcc -o cwse_ide cwse_ide.c \
 *       $(pkg-config --cflags --libs gtk4 vte-2.91-gtk4) \
 *       -Wall -Wextra -O2
 *
 * Dependencies (Debian/Ubuntu):
 *   sudo apt install libgtk-4-dev libvte-2.91-gtk4-dev
 *
 * Dependencies (Fedora/RHEL):
 *   sudo dnf install gtk4-devel vte291-gtk4-devel
 *
 * Usage:
 *   ./cwse_ide [initial_folder]
 *
 * The ./cwse binary must be present in the current directory
 * (or in the PATH if CWSE_BIN is adjusted).
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
#include "logo_data.h"

/* ─────────────────────────────────────────────
   Configuration
   ───────────────────────────────────────────── */
#define APP_ID        "org.cwse.ide"
#define CWSE_BIN      "./cwse"
#define SIDEBAR_WIDTH 230
#define FONT_TERMINAL "Monospace 12"


int run_cwse_engine(int argc, char **argv);

/* ─────────────────────────────────────────────
   GtkListStore Columns
   ───────────────────────────────────────────── */
enum {
    COL_ICON,       /* icon-name (char*)  */
    COL_NAME,       /* nom affiché        */
    COL_FULLPATH,   /* chemin complet     */
    COL_IS_DIR,     /* gboolean           */
    N_COLS
};

/* ─────────────────────────────────────────────
   Global Application State
   ───────────────────────────────────────────── */
typedef struct {
    GtkApplication *gapp;

    /* Main window */
    GtkWidget      *window;

    /* Sidebar panel */
    GtkWidget      *sidebar_box;
    GtkWidget      *path_label;
    GtkWidget      *tree_view;
    GtkListStore   *store;

    /* VTE Terminal */
    GtkWidget      *terminal;

    /* Status bar */
    GtkWidget      *status_label;
    GtkWidget      *cwse_status_label;

    /* Current working directory of file browser */
    char            cwd[PATH_MAX];

    /* Active file */
    char            active_file[PATH_MAX];
} App;

static App G = {0};  /* singleton */

/* ─────────────────────────────────────────────
   Catppuccin Mocha Theme — Complete Palette
   ───────────────────────────────────────────── */
static const GdkRGBA PALETTE[16] = {
    {0.094, 0.094, 0.133, 1.0},   /*  0 black       #181825 */
    {0.949, 0.357, 0.443, 1.0},   /*  1 red         #f38ba8 */
    {0.651, 0.890, 0.631, 1.0},   /*  2 green       #a6e3a1 */
    {0.973, 0.784, 0.455, 1.0},   /*  3 yellow      #f9e2af */
    {0.537, 0.706, 0.980, 1.0},   /*  4 blue        #89b4fa */
    {0.796, 0.651, 0.969, 1.0},   /*  5 magenta     #cba6f7 */
    {0.529, 0.886, 0.878, 1.0},   /*  6 cyan        #89dceb */
    {0.804, 0.839, 0.957, 1.0},   /*  7 white       #cdd6f4 */
    {0.365, 0.369, 0.451, 1.0},   /*  8 br.black    #585b70 */
    {0.949, 0.357, 0.443, 1.0},   /*  9 br.red      #f38ba8 */
    {0.651, 0.890, 0.631, 1.0},   /* 10 br.green    #a6e3a1 */
    {0.973, 0.784, 0.455, 1.0},   /* 11 br.yellow   #f9e2af */
    {0.537, 0.706, 0.980, 1.0},   /* 12 br.blue     #89b4fa */
    {0.796, 0.651, 0.969, 1.0},   /* 13 br.magenta  #cba6f7 */
    {0.529, 0.886, 0.878, 1.0},   /* 14 br.cyan     #89dceb */
    {0.804, 0.839, 0.957, 1.0},   /* 15 br.white    #cdd6f4 */
};
static const GdkRGBA COL_FG  = {0.804, 0.839, 0.957, 1.0}; /* #cdd6f4 */
static const GdkRGBA COL_BG  = {0.118, 0.118, 0.169, 1.0}; /* #1e1e2e */

/* ─────────────────────────────────────────────
   GTK Interface CSS
   ───────────────────────────────────────────── */
static const char *APP_CSS =
    "window { background: #1e1e1e; }"

    /* Header bar : Gris anthracite profond */
    "headerbar {"
    "  background: #252525;"
    "  border-bottom: 1px solid #333333;"
    "  color: #d4d4d4;"
    "  padding: 0px 8px;"
    "}"
    "headerbar .title { "
    "  color: #cccccc; "
    "  font-weight: 500; "
    "  font-size: 13px; "
    "}"

    /* Boutons de la barre d'outils : Monochrome */
    "button.toolbar-btn {"
    "  background: transparent;"
    "  color: #969696;"
    "  border: none;"
    "  border-radius: 3px;"
    "  padding: 6px 12px;"
    "}"
    "button.toolbar-btn:hover {"
    "  background: #3c3c3c;"
    "  color: #ffffff;"
    "}"
    "button.toolbar-btn:active { background: #323232; }"

    /* Bouton Run : Désaturé pour rester pro */
    "button.run-btn {"
    "  background: #4a4a4a;"
    "  color: #ffffff;"
    "  font-weight: 500;"
    "  border: 1px solid #555555;"
    "  border-radius: 3px;"
    "  padding: 5px 12px;"
    "}"
    "button.run-btn:hover { background: #5a5a5a; }"

    /* Sidebar */
    ".sidebar {"
    "  background: #252526;"
    "  border-right: 1px solid #333333;"
    "}"

    /* Path bar : Noir mat */
    ".path-bar {"
    "  background: #1a1a1a;"
    "  padding: 4px 10px;"
    "  border-bottom: 1px solid #333333;"
    "}"
    ".path-bar label {"
    "  color: #aaaaaa;"
    "  font-family: monospace;"
    "  font-size: 11px;"
    "}"

    /* Section title */
    ".section-label {"
    "  color: #666666;"
    "  font-size: 10px;"
    "  font-weight: 700;"
    "  padding: 8px 10px 4px 10px;"
    "  text-transform: uppercase;"
    "}"

    /* File tree */
    "treeview {"
    "  background: #252526;"
    "  color: #cccccc;"
    "  font-family: monospace;"
    "  font-size: 12px;"
    "}"
    "treeview:selected {"
    "  background: #37373d;"
    "  color: #ffffff;"
    "}"
    "treeview header button {"
    "  background: #1e1e1e;"
    "  color: #888888;"
    "  border: none;"
    "  border-bottom: 1px solid #333333;"
    "}"

    /* GtkPaned separator */
    "paned > separator {"
    "  background: #333333;"
    "  min-width: 1px;"
    "}"

    /* Status bar */
    ".status-bar {"
    "  background: #007acc;" /* Seule touche de couleur : le bleu classique des status bars */
    "  padding: 3px 10px;"
    "}"
    ".status-bar label {"
    "  color: #ffffff;"
    "  font-size: 11px;"
    "}"
    /* Si tu veux la barre de statut grise aussi, remplace #007acc par #1e1e1e */

    /* Active file badge */
    ".active-badge {"
    "  background: #3c3c3c;"
    "  color: #cccccc;"
    "  font-family: monospace;"
    "  font-size: 11px;"
    "  border-radius: 2px;"
    "  padding: 1px 6px;"
    "  border: 1px solid #454545;"
    "}"
    ;

/* ─────────────────────────────────────────────
   Utilities
   ───────────────────────────────────────────── */
static int _cmp_name(gconstpointer a, gconstpointer b) {
    return g_ascii_strcasecmp(*(const char **)a, *(const char **)b);
}

static const char *file_icon(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return "text-x-generic-symbolic";
    if (!g_ascii_strcasecmp(ext, ".cbl") ||
        !g_ascii_strcasecmp(ext, ".cob") ||
        !g_ascii_strcasecmp(ext, ".cpy"))
        return "text-x-script-symbolic";
    if (!g_ascii_strcasecmp(ext, ".c")   ||
        !g_ascii_strcasecmp(ext, ".h"))
        return "text-x-csrc-symbolic";
    if (!g_ascii_strcasecmp(ext, ".mk")  ||
        !g_ascii_strcasecmp(ext, "akefile"))
        return "applications-engineering-symbolic";
    return "text-x-generic-symbolic";
}

static void set_status(const char *msg, const char *css_class) {
    gtk_label_set_text(GTK_LABEL(G.status_label), msg);
    /* Remove old classes */
    gtk_widget_remove_css_class(G.status_label, "active");
    gtk_widget_remove_css_class(G.status_label, "error");
    if (css_class)
        gtk_widget_add_css_class(G.status_label, css_class);
}

/* ─────────────────────────────────────────────
   Spawn ./cwse in VTE terminal
   ───────────────────────────────────────────── */
static void spawn_cwse(const char *filepath) {
    vte_terminal_reset(VTE_TERMINAL(G.terminal), TRUE, TRUE);

    /* Mise à jour des labels de l'interface */
    if (filepath) {
        strncpy(G.active_file, filepath, PATH_MAX - 1);
        char label[PATH_MAX + 20];
        snprintf(label, sizeof label, " ✎  %s", filepath);
        gtk_label_set_text(GTK_LABEL(G.cwse_status_label), label);
        set_status(" CWSE opened", "active");
    } else {
        G.active_file[0] = '\0';
        gtk_label_set_text(GTK_LABEL(G.cwse_status_label), "  (no file)");
        set_status(" CWSE started - double-click a file to edit", NULL);
    }

    /* --- C'EST ICI QUE TU METS LE CODE --- */
    
    // On utilise "/proc/self/exe" pour que le programme se relance lui-même
    const char *self_exe = "/proc/self/exe"; 
    const char *spawn_argv[4];
    
    spawn_argv[0] = self_exe;           // Le programme actuel
    spawn_argv[1] = "--embed-engine";  // Le flag qu'on a ajouté dans le main()
    spawn_argv[2] = filepath;          // Le fichier à ouvrir (peut être NULL)
    spawn_argv[3] = NULL;              // Fin de la liste

    vte_terminal_spawn_async(
        VTE_TERMINAL(G.terminal),   // Le terminal cible
        VTE_PTY_DEFAULT,            // Type de PTY
        G.cwd,                      // Dossier de travail
        (char**)spawn_argv,         // La commande (soit lui-même)
        NULL,                       // Environnement (hérité)
        G_SPAWN_SEARCH_PATH,        // Flags
        NULL, NULL, NULL,           // Callbacks de setup
        -1,                         // Timeout
        NULL, NULL, NULL            // Callback de fin
    );

    /* Redonner le focus au clavier pour pouvoir taper dans ncurses */
    gtk_widget_grab_focus(G.terminal);
}

/* ─────────────────────────────────────────────
   Fill File Tree
   ───────────────────────────────────────────── */
static void populate_tree(const char *dirpath) {
    strncpy(G.cwd, dirpath, PATH_MAX - 1);
    gtk_label_set_text(GTK_LABEL(G.path_label), dirpath);
    gtk_list_store_clear(G.store);

    DIR *d = opendir(dirpath);
    if (!d) {
        set_status(" Cannot open folder", "error");
        return;
    }

    GPtrArray *dirs  = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        /* Skip hidden entries except ".." */
        if (ent->d_name[0] == '.' && strcmp(ent->d_name, "..") != 0)
            continue;
        if (strcmp(ent->d_name, ".") == 0)
            continue;

        char full[PATH_MAX];
        snprintf(full, sizeof full, "%s/%s", dirpath, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode))
            g_ptr_array_add(dirs,  g_strdup(ent->d_name));
        else
            g_ptr_array_add(files, g_strdup(ent->d_name));
    }
    closedir(d);

    g_ptr_array_sort(dirs,  _cmp_name);
    g_ptr_array_sort(files, _cmp_name);

    GtkTreeIter iter;

    /* Directories first */
    for (guint i = 0; i < dirs->len; i++) {
        const char *n = (const char *)dirs->pdata[i];
        const char *ic = strcmp(n, "..") == 0
                        ? "go-up-symbolic"
                        : "folder-symbolic";
        char fp[PATH_MAX];
        snprintf(fp, sizeof fp, "%s/%s", dirpath, n);

        gtk_list_store_append(G.store, &iter);
        gtk_list_store_set(G.store, &iter,
            COL_ICON,    ic,
            COL_NAME,    n,
            COL_FULLPATH, fp,
            COL_IS_DIR,  TRUE,
            -1);
    }

    /* Files afterwards */
    for (guint i = 0; i < files->len; i++) {
        const char *n = (const char *)files->pdata[i];
        char fp[PATH_MAX];
        snprintf(fp, sizeof fp, "%s/%s", dirpath, n);

        gtk_list_store_append(G.store, &iter);
        gtk_list_store_set(G.store, &iter,
            COL_ICON,    file_icon(n),
            COL_NAME,    n,
            COL_FULLPATH, fp,
            COL_IS_DIR,  FALSE,
            -1);
    }

    g_ptr_array_free(dirs,  TRUE);
    g_ptr_array_free(files, TRUE);
}

/* ─────────────────────────────────────────────
   Signal: Double-click on tree
   ───────────────────────────────────────────── */
static void on_row_activated(GtkTreeView *tv, GtkTreePath *tp,
                              GtkTreeViewColumn *col, gpointer ud)
{
    (void)col; (void)ud;
    GtkTreeModel *model = gtk_tree_view_get_model(tv);
    GtkTreeIter   iter;
    if (!gtk_tree_model_get_iter(model, &iter, tp)) return;

    char     *fullpath = NULL;
    gboolean  is_dir   = FALSE;
    gtk_tree_model_get(model, &iter,
        COL_FULLPATH, &fullpath,
        COL_IS_DIR,   &is_dir,
        -1);

    if (!fullpath) return;

    if (is_dir) {
        populate_tree(fullpath);
    } else {
        spawn_cwse(fullpath);
    }
    g_free(fullpath);
}

/* ─────────────────────────────────────────────
   Signal: CWSE exit
   ───────────────────────────────────────────── */
static void on_child_exited(VteTerminal *term, int status, gpointer ud) {
    (void)term; (void)status; (void)ud;
    G.active_file[0] = '\0';
    gtk_label_set_text(GTK_LABEL(G.cwse_status_label), "  (no file)");
    set_status(" CWSE terminated - double-click a file to relaunch", NULL);
}

/* ─────────────────────────────────────────────
   Dialog: New File
   ───────────────────────────────────────────── */
typedef struct { GtkWidget *dlg; GtkWidget *entry; } NewFileDlg;

static void on_new_file_response(GtkDialog *dlg, int resp, gpointer ud) {
    NewFileDlg *nfd = (NewFileDlg *)ud;
    if (resp == GTK_RESPONSE_ACCEPT) {
        const char *name = gtk_editable_get_text(GTK_EDITABLE(nfd->entry));
        if (name && *name) {
            char fp[PATH_MAX];
            snprintf(fp, sizeof fp, "%s/%s", G.cwd, name);
            /* Create file if it doesn't exist */
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
        "New COBOL File",
        GTK_WINDOW(G.window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Create",   GTK_RESPONSE_ACCEPT,
        NULL);

    GtkWidget *ca  = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box,   18);
    gtk_widget_set_margin_end(box,     18);
    gtk_widget_set_margin_top(box,     14);
    gtk_widget_set_margin_bottom(box,  10);

    GtkWidget *lbl = gtk_label_new("File name:");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);

    GtkWidget *entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), "program.cbl");
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);

    gtk_box_append(GTK_BOX(box), lbl);
    gtk_box_append(GTK_BOX(box), entry);
    gtk_box_append(GTK_BOX(ca),  box);

    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);

    NewFileDlg *nfd = g_new(NewFileDlg, 1);
    nfd->dlg   = dlg;
    nfd->entry = entry;

    g_signal_connect(dlg, "response", G_CALLBACK(on_new_file_response), nfd);
    gtk_widget_show(dlg);
}

/* ─────────────────────────────────────────────
   Dialog: Open Folder
   ───────────────────────────────────────────── */
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
        "Open Folder",
        GTK_WINDOW(G.window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open",  GTK_RESPONSE_ACCEPT,
        NULL);

    /* Position on current folder */
    GFile *cur = g_file_new_for_path(G.cwd);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), cur, NULL);
    g_object_unref(cur);

    g_signal_connect(dlg, "response", G_CALLBACK(on_folder_dlg_response), NULL);
    gtk_widget_show(dlg);
}

/* ─────────────────────────────────────────────
   Button: Refresh Tree
   ───────────────────────────────────────────── */
static void on_refresh_clicked(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;
    populate_tree(G.cwd);
}

/* ─────────────────────────────────────────────
   Button: Launch CWSE without File
   ───────────────────────────────────────────── */
static void on_launch_cwse_clicked(GtkButton *btn, gpointer ud) {
    (void)btn; (void)ud;
    spawn_cwse(NULL);
}

/* ─────────────────────────────────────────────
   Construction du panneau latéral
   ───────────────────────────────────────────── */
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

    /* Étiquette "FICHIERS" */
    GtkWidget *sec_label = gtk_label_new("FILES");
    gtk_widget_add_css_class(sec_label, "section-label");
    gtk_label_set_xalign(GTK_LABEL(sec_label), 0.0f);
    gtk_box_append(GTK_BOX(box), sec_label);

    /* Liste des fichiers */
    G.store = gtk_list_store_new(N_COLS,
        G_TYPE_STRING,   /* COL_ICON     */
        G_TYPE_STRING,   /* COL_NAME     */
        G_TYPE_STRING,   /* COL_FULLPATH */
        G_TYPE_BOOLEAN   /* COL_IS_DIR   */
    );

    GtkWidget *tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(G.store));
    G.tree_view   = tv;
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tv), FALSE);
    gtk_tree_view_set_activate_on_single_click(GTK_TREE_VIEW(tv), FALSE);
    gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(tv), FALSE);

    /* Colonne icône */
    GtkCellRenderer    *icon_r   = gtk_cell_renderer_pixbuf_new();
    g_object_set(icon_r, "stock-size", GTK_ICON_SIZE_NORMAL, NULL);
    GtkTreeViewColumn  *icon_col = gtk_tree_view_column_new_with_attributes(
        "ICON", icon_r, "icon-name", COL_ICON, NULL);
    gtk_tree_view_column_set_min_width(icon_col, 26);
    gtk_tree_view_column_set_sizing(icon_col, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), icon_col);

    /* Colonne nom */
    GtkCellRenderer    *name_r   = gtk_cell_renderer_text_new();
    GtkTreeViewColumn  *name_col = gtk_tree_view_column_new_with_attributes(
        "NAME", name_r, "text", COL_NAME, NULL);
    gtk_tree_view_column_set_expand(name_col, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), name_col);

    g_signal_connect(tv, "row-activated", G_CALLBACK(on_row_activated), NULL);

    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), tv);
    gtk_widget_set_vexpand(sw, TRUE);
    gtk_box_append(GTK_BOX(box), sw);

    return box;
}

/* ─────────────────────────────────────────────
   Construction du panneau terminal
   ───────────────────────────────────────────── */
static GtkWidget *build_terminal_pane(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* VTE */
    G.terminal = vte_terminal_new();

    /* 1. Couleurs de base du terminal */
    vte_terminal_set_colors(VTE_TERMINAL(G.terminal),
        &COL_FG, &COL_BG, PALETTE, G_N_ELEMENTS(PALETTE));

    /* 2. CONFIGURATION DU CURSEUR POUR UNE VISIBILITÉ MAXIMALE */
    /* On définit un orange vif pour le bloc du curseur */
    GdkRGBA cursor_bg = {1.0, 0.65, 0.0, 1.0}; 
    /* On définit du noir pour le texte qui se trouve sous le curseur */
    GdkRGBA cursor_fg = {0.0, 0.0, 0.0, 1.0};
    
    vte_terminal_set_color_cursor(VTE_TERMINAL(G.terminal), &cursor_bg);
    vte_terminal_set_color_cursor_foreground(VTE_TERMINAL(G.terminal), &cursor_fg);

    /* 3. COMPORTEMENT DU CURSEUR */
    /* VTE_CURSOR_BLINK_OFF : le curseur ne disparaît jamais (ne clignote pas) */
    vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(G.terminal), VTE_CURSOR_BLINK_OFF);
    /* VTE_CURSOR_SHAPE_BLOCK : un gros rectangle plein */
    vte_terminal_set_cursor_shape(VTE_TERMINAL(G.terminal), VTE_CURSOR_SHAPE_BLOCK);

    /* Police */
    PangoFontDescription *font = pango_font_description_from_string(FONT_TERMINAL);
    vte_terminal_set_font(VTE_TERMINAL(G.terminal), font);
    pango_font_description_free(font);

    /* Autres paramètres */
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(G.terminal), 10000);
    vte_terminal_set_mouse_autohide(VTE_TERMINAL(G.terminal), TRUE);
    vte_terminal_set_rewrap_on_resize(VTE_TERMINAL(G.terminal), TRUE);

    g_signal_connect(G.terminal, "child-exited",
                     G_CALLBACK(on_child_exited), NULL);

    gtk_widget_set_vexpand(G.terminal, TRUE);
    gtk_widget_set_hexpand(G.terminal, TRUE);
    gtk_box_append(GTK_BOX(box), G.terminal);

    /* Barre de statut inférieure */
    GtkWidget *status_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(status_row, "status-bar");

    G.status_label = gtk_label_new(" READY");
    gtk_label_set_xalign(GTK_LABEL(G.status_label), 0.0f);
    gtk_widget_set_hexpand(G.status_label, TRUE);
    gtk_box_append(GTK_BOX(status_row), G.status_label);

    G.cwse_status_label = gtk_label_new("  (no file)");
    gtk_widget_add_css_class(G.cwse_status_label, "active-badge");
    gtk_box_append(GTK_BOX(status_row), G.cwse_status_label);

    GtkWidget *padding = gtk_label_new(" ");
    gtk_box_append(GTK_BOX(status_row), padding);

    gtk_box_append(GTK_BOX(box), status_row);

    return box;
}

/* ─────────────────────────────────────────────
   Actions: Settings menu
   ───────────────────────────────────────────── */

static void on_show_credits(GSimpleAction *action, GVariant *param, gpointer app) {
    (void)action; (void)param; (void)app;

    GtkWidget *dlg = gtk_dialog_new();
    gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(G.window));
    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);

    /* Pas de décorations */
    gtk_window_set_decorated(GTK_WINDOW(dlg), FALSE);

    /* Taille minimale (optionnel mais propre) */
    gtk_window_set_default_size(GTK_WINDOW(dlg), 360, -1);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);

    /* 👉 PADDING */
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);

    /* Titre */
    GtkWidget *title = gtk_label_new("CWSE IDE");
    gtk_widget_add_css_class(title, "title-2");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

    /* Texte */
    GtkWidget *label = gtk_label_new(
        "Ardoise COBOL IDE\n\n"
        "Created by Ento9\n"
    );
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);

    /* Bouton */
    GtkWidget *btn = gtk_button_new_with_label("Close");
    gtk_widget_set_halign(btn, GTK_ALIGN_END);

    g_signal_connect_swapped(btn, "clicked",
        G_CALLBACK(gtk_window_destroy), dlg);

    /* Layout */
    gtk_box_append(GTK_BOX(box), title);
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), btn);
    gtk_box_append(GTK_BOX(content), box);

    gtk_window_present(GTK_WINDOW(dlg));
}

static void on_show_terms(GSimpleAction *action, GVariant *param, gpointer app) {
    (void)action; (void)param; (void)app;

    GtkWidget *dlg = gtk_dialog_new();
    gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(G.window));
    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);

    /* Enlever décorations */
    gtk_window_set_decorated(GTK_WINDOW(dlg), FALSE);

    /* Contenu */
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    /* 👉 PADDING ICI */
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);

    /* Texte */
    GtkWidget *label = gtk_label_new(
        "Terms & Conditions\n\nArdoise COBOL IDE\nis under the MIT License."
    );
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);

    /* Bouton */
    GtkWidget *btn = gtk_button_new_with_label("Close");
    gtk_widget_set_halign(btn, GTK_ALIGN_END);

    g_signal_connect_swapped(btn, "clicked",
        G_CALLBACK(gtk_window_destroy), dlg);

    /* Layout */
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), btn);
    gtk_box_append(GTK_BOX(content), box);

    gtk_window_present(GTK_WINDOW(dlg));
}

/* ─────────────────────────────────────────────
   Construction de la barre d'en-tête
   ───────────────────────────────────────────── */
static GtkWidget *build_headerbar(void) {
    GtkWidget *hb = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hb), TRUE);

    GtkWidget *title_lbl = gtk_label_new("Ardoise COBOL IDE");
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(hb), title_lbl);

    /* ── Côté gauche : outils fichier ── */
    GtkWidget *lbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_set_spacing(GTK_BOX(lbox), 0);

    GtkWidget *new_btn = gtk_button_new_with_label("New");
    gtk_widget_add_css_class(new_btn, "toolbar-btn");
    gtk_widget_set_tooltip_text(new_btn, "New COBOL File");
    g_signal_connect(new_btn, "clicked", G_CALLBACK(on_new_file_clicked), NULL);
    gtk_box_append(GTK_BOX(lbox), new_btn);

    GtkWidget *open_btn = gtk_button_new_with_label("Open");
    gtk_widget_add_css_class(open_btn, "toolbar-btn");
    gtk_widget_set_tooltip_text(open_btn, "Open Folder");
    g_signal_connect(open_btn, "clicked", G_CALLBACK(on_open_folder_clicked), NULL);
    gtk_box_append(GTK_BOX(lbox), open_btn);

    GtkWidget *ref_btn = gtk_button_new_with_label("Refresh");
    gtk_widget_add_css_class(ref_btn, "toolbar-btn");
    gtk_widget_set_tooltip_text(ref_btn, "Refresh Tree View");
    g_signal_connect(ref_btn, "clicked", G_CALLBACK(on_refresh_clicked), NULL);
    gtk_box_append(GTK_BOX(lbox), ref_btn);

    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), lbox);

    /* ── Côté droit : lancer CWSE ── */
    GtkWidget *run_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_set_spacing(GTK_BOX(run_box), 0);

    GtkWidget *run_btn = gtk_button_new_with_label("▶ Launch CWSE");
    gtk_widget_add_css_class(run_btn, "run-btn");
    gtk_widget_set_tooltip_text(run_btn, "Launch CWSE (without file)");
    g_signal_connect(run_btn, "clicked", G_CALLBACK(on_launch_cwse_clicked), NULL);
    gtk_box_append(GTK_BOX(run_box), run_btn);

    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), run_box);

        /* ── Menu Settings (☰) ── */
    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Credits", "app.credits");
    g_menu_append(menu, "Terms & Conditions", "app.terms");

    GtkWidget *menu_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_btn), G_MENU_MODEL(menu));

    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), menu_btn);

    return hb;
}

/* ─────────────────────────────────────────────
   Activation de l'application
   ───────────────────────────────────────────── */
static void on_activate(GApplication *gapp, gpointer ud) {
    (void)ud;

    /* ── CSS global ── */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, APP_CSS);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    /* ── Fenêtre principale ── */
    G.window = gtk_application_window_new(GTK_APPLICATION(gapp));
    gtk_window_set_title(GTK_WINDOW(G.window), "Ardoise COBOL IDE");
    gtk_window_set_default_size(GTK_WINDOW(G.window), 1280, 800);

    /* ── Configuration de l'icône (Barre des tâches uniquement) ── */
    // On définit un ID d'application pour que le Window Manager fasse le lien
    g_set_prgname("ardoise-cobol-ide");
    
    // On définit l'icône par défaut pour toutes les fenêtres de l'app
    // à partir de la ressource "/logo.png" compilée.
    gtk_window_set_default_icon_name("ardoise-cobol-ide");
    
    // Application de l'icône à la fenêtre actuelle
    // Note: Utilise le nom déclaré dans le fichier .desktop ou les ressources
    gtk_window_set_icon_name(GTK_WINDOW(G.window), "ardoise-cobol-ide");

    /* ── Header (Sans logo ajouté) ── */
    gtk_window_set_titlebar(GTK_WINDOW(G.window), build_headerbar());

    /* ── Layout principal : GtkPaned horizontal ── */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(paned), SIDEBAR_WIDTH);
    g_object_set(paned,
        "resize-start-child", FALSE,
        "resize-end-child",   TRUE,
        "shrink-start-child", FALSE,
        "shrink-end-child",   FALSE,
        NULL);

    GtkWidget *sidebar   = build_sidebar();
    GtkWidget *term_pane = build_terminal_pane();

    gtk_paned_set_start_child(GTK_PANED(paned), sidebar);
    gtk_paned_set_end_child(GTK_PANED(paned),   term_pane);

    gtk_window_set_child(GTK_WINDOW(G.window), paned);

    /* ── Répertoire initial ── */
    if (G.cwd[0] == '\0') {
        if (!getcwd(G.cwd, PATH_MAX - 1))
            strncpy(G.cwd, ".", PATH_MAX - 1);
    }
    populate_tree(G.cwd);

    /* ── Actions et Lancement ── */
    spawn_cwse(NULL);
    
    GSimpleAction *credits_action = g_simple_action_new("credits", NULL);
    g_signal_connect(credits_action, "activate", G_CALLBACK(on_show_credits), NULL);
    g_action_map_add_action(G_ACTION_MAP(G.gapp), G_ACTION(credits_action));

    GSimpleAction *terms_action = g_simple_action_new("terms", NULL);
    g_signal_connect(terms_action, "activate", G_CALLBACK(on_show_terms), NULL);
    g_action_map_add_action(G_ACTION_MAP(G.gapp), G_ACTION(terms_action));
    
    gtk_window_present(GTK_WINDOW(G.window));
}

/* ─────────────────────────────────────────────
   Point d'entrée
   ───────────────────────────────────────────── */
int main(int argc, char **argv) {
    // Si l'application est appelée avec le nom "cwse_internal"
    // ou un argument caché, on lance le moteur ncurses
    if (argc > 1 && strcmp(argv[1], "--embed-engine") == 0) {
        // On décale les arguments pour le moteur
        return run_cwse_engine(argc - 1, &argv[1]);
    }

    // Sinon, on lance l'interface GTK normale
    G.gapp = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(G.gapp, "activate", G_CALLBACK(on_activate), NULL);

    int rc = g_application_run(G_APPLICATION(G.gapp), argc, argv);
    g_object_unref(G.gapp);
    return rc;
}
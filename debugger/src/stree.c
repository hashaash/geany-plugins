/*
 *		stree.c
 *      
 *      Copyright 2010 Alexander Petukhov <devel(at)apetukhov.ru>
 *      
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *      
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *      
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

/*
 *		Contains function to manipulate stack trace tree view.
 */

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>

#include "geanyplugin.h"

#include "breakpoints.h"
#include "utils.h"
#include "breakpoint.h"
#include "debug_module.h"

#include "xpm/frame_current.xpm"

/* columns minumum width in characters */
#define MW_ADRESS	10
#define MW_FUNCTION	10
#define MW_FILE		0
#define MW_LINE		4

#define ARROW_PADDING 10

/* Tree view columns */
enum
{
   S_ARROW,
   S_ADRESS,
   S_FUNCTION,
   S_FILEPATH,
   S_LINE,
   S_N_COLUMNS
};

/* hash table to remember whether source file fo stack frame exists */
GHashTable* frames = NULL;

/* double click callback pointer */
static move_to_line_cb callback = NULL;

/* tree view, model and store handles */
static GtkWidget* tree = NULL;
static GtkTreeModel* model = NULL;
static GtkListStore* store = NULL;

/* flag to indicate whether to handle selection change */
static gboolean handle_selection = TRUE;

/* pixbuf with the frame arrow */
GdkPixbuf *arrow_pixbuf = NULL;

/*
 *  Handles same tree row click to open frame position
 */
static gboolean on_msgwin_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	if (event->type == GDK_BUTTON_PRESS)
	{
		GtkTreePath *pressed_path = NULL;
		if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tree), (int)event->x, (int)event->y, &pressed_path, NULL, NULL, NULL))
		{
			GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
			GList *rows = gtk_tree_selection_get_selected_rows(selection, &model);
			GtkTreePath *selected_path = (GtkTreePath*)rows->data;

			if (!gtk_tree_path_compare(pressed_path, selected_path))
			{
				GtkTreeIter iter;
				gtk_tree_model_get_iter (
					 model,
					 &iter,
					 pressed_path);

				gchar *file;
				int line;
				gtk_tree_model_get (
					model,
					&iter,
					S_FILEPATH, &file,
					S_LINE, &line,
					-1);
				
				/* check if file name is not empty and we have source files for the frame */
				if (strlen(file) && GPOINTER_TO_INT(g_hash_table_lookup(frames, (gpointer)file)))
					callback(file, line);
				
				g_free(file);
			}

			g_list_foreach (rows, (GFunc) gtk_tree_path_free, NULL);
			g_list_free (rows);

			gtk_tree_path_free(pressed_path);
		}
	}

	return FALSE;
}

/*
 *  Tree view selection changed callback
 */
void on_selection_changed(GtkTreeSelection *treeselection, gpointer user_data)
{
	if (handle_selection)
	{
		GList *rows = gtk_tree_selection_get_selected_rows(treeselection, &model);
		GtkTreePath *path = (GtkTreePath*)rows->data;
		
		GtkTreeIter iter;
		gtk_tree_model_get_iter (
			 gtk_tree_view_get_model(GTK_TREE_VIEW(tree)),
			 &iter,
			 path);
		gchar *file;
		int line;
		gtk_tree_model_get (
			gtk_tree_view_get_model(GTK_TREE_VIEW(tree)),
			&iter,
			S_FILEPATH, &file,
			S_LINE, &line,
			-1);
		
		/* check if file name is not empty and we have source files for the frame */
		if (strlen(file) && GPOINTER_TO_INT(g_hash_table_lookup(frames, (gpointer)file)))
			callback(file, line);
		
		g_free(file);

		gtk_tree_path_free(path);
		g_list_free(rows);
	}
}

/*
 *	inits stack trace tree
 *	arguments:
 * 		cb - callback to call on double click	
 */
gboolean stree_init(move_to_line_cb cb)
{
	callback = cb;
	
	arrow_pixbuf = gdk_pixbuf_new_from_xpm_data(frame_current_xpm);

	/* create tree view */
	store = gtk_list_store_new (
		S_N_COLUMNS,
		GDK_TYPE_PIXBUF,
		G_TYPE_STRING,
		G_TYPE_STRING,
		G_TYPE_STRING,
		G_TYPE_INT);
	model = GTK_TREE_MODEL(store);
	tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL(store));
	
	/* set tree view properties */
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), 1);
	g_object_set(tree, "rules-hint", TRUE, NULL);

	/* connect signals */
	g_signal_connect(G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree))), "changed",
	                 G_CALLBACK (on_selection_changed), NULL);

	/* for clicking on already selected frame */
	g_signal_connect(tree, "button-press-event",
					G_CALLBACK(on_msgwin_button_press), NULL);

	/* creating columns */
	GtkCellRenderer		*renderer;
	GtkTreeViewColumn	*column;
	const gchar			*header;
	
	int	char_width = get_char_width(tree);

	/* arrow */
	renderer = gtk_cell_renderer_pixbuf_new ();
	column = create_column("", renderer, FALSE,
		gdk_pixbuf_get_width(arrow_pixbuf) + 2 * ARROW_PADDING,
		"pixbuf", S_ARROW);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);

	/* adress */
	header = _("Address");
	renderer = gtk_cell_renderer_text_new ();
	column = create_column(header, renderer, FALSE,
		get_header_string_width(header, MW_ADRESS, char_width),
		"text", S_ADRESS);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);

	/* function */
	header = _("Function");
	renderer = gtk_cell_renderer_text_new ();
	column = create_column(header, renderer, FALSE,
		get_header_string_width(header, MW_FUNCTION, char_width),
		"text", S_FUNCTION);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);
	
	/* file */
	header = _("File");
	renderer = gtk_cell_renderer_text_new ();
	column = create_column(header, renderer, TRUE,
		get_header_string_width(header, MW_FILE, char_width),
		"text", S_FILEPATH);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);
	
	/* line */
	header = _("Line");
	renderer = gtk_cell_renderer_text_new ();
	column = create_column(header, renderer, FALSE,
		get_header_string_width(header, MW_LINE, char_width),
		"text", S_LINE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);

	/* create frames hash table */
	frames =  g_hash_table_new_full(
		g_str_hash,
		g_str_equal,
		(GDestroyNotify)g_free,
		NULL);
		
	return TRUE;
}

/*
 *	get stack trace tree view
 */
GtkWidget* stree_get_widget()
{
	return tree;
}

/*
 *	add frame to the tree view
 */
void stree_add(frame *f, gboolean first)
{
	GtkTreeIter iter;
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
                    S_ADRESS, f->address,
                    S_FUNCTION, f->function,
                    S_FILEPATH, f->file,
                    S_LINE, f->line,
                    -1);
                    
	if (first)
	{
		gtk_list_store_set (store, &iter,
						S_ARROW, arrow_pixbuf,
						-1);
	}
    
	/* remember if we have source for this frame */
    if (f->have_source && !GPOINTER_TO_INT(g_hash_table_lookup(frames, (gpointer)f->file)))
		g_hash_table_insert(frames, g_strdup(f->file), GINT_TO_POINTER(f->have_source));
}

/*
 *	clear tree view
 */
void stree_clear()
{
	handle_selection = FALSE;
	
	gtk_list_store_clear(store);
	g_hash_table_remove_all(frames);

	handle_selection = TRUE;
}

/*
 *	select first item in tree view
 */
void stree_select_first()
{
	GtkTreePath* path = gtk_tree_path_new_first();
	
	gtk_tree_selection_select_path (
		gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)),
		path);
	
	gtk_tree_path_free(path);
}

/*
 *	called on plugin exit to free arrow pixbuffer
 */
void stree_destroy()
{
	g_object_unref(arrow_pixbuf);
}

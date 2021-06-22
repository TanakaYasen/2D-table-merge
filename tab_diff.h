#ifndef _TAB_DIFF_H
#define _TAB_DIFF_H

#include "tab_data.h"

struct col_mod {
	tab_modify_type_t mod_type;
	const tab_cell_t	*cell;
	const tab_cell_t	*cell_old;
};

struct row_mod {
	tab_modify_type_t mod_type;
	struct col_mod	*col;
};

typedef struct diff_desc {
	struct col_mod *md_hdr;
	struct row_mod *md_array;
	int ncols;
	int nrows;
} diff_desc_t;

diff_desc_t *tab_diff_generate(const tab_desc_t *tab_old, const tab_desc_t *tab_new);
void		tab_diff_free(diff_desc_t *differ);

#endif

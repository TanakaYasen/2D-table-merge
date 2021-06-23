#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "tab_diff.h"
#include "core_algorithm.h"
#include "tab_log.h"

#include "ks/3rd/uthash.h"

#define tab_alloc		malloc
#define tab_free		free
#define tab_realloc		realloc

static int change_step[MAX_LEN];
static const tab_cell_t *one_col_old[MAX_LEN];
static const tab_cell_t *one_col_new[MAX_LEN];

typedef struct tab_diff {
	tab_modify_type_t	mod_type;
	int ref_idx_old;
	int ref_idx_new;
}tab_diff_t;

static tab_diff_t sd[MAX_LEN];
static tab_diff_t ad[MAX_LEN];

static int  generate_diff_cols(const tab_desc_t *old, const tab_desc_t *new) {
	int step = 0;
	int n_edit = ld_path_of(old->hdr_cell, old->col, new->hdr_cell, new->col, cell_cmp, change_step, &step);

	int si = 0;
	int d_o = 0, d_n = 0;
	for (int i = step-1; i >= 0; i--, si++) {
		int dir = change_step[i];
		tab_diff_t *psd = sd + si;
		switch (dir) {
		case DIR_LU:
			psd->mod_type = TAB_MOD_NONE;
			psd->ref_idx_old = d_o;
			psd->ref_idx_new = d_n;
			//printf("*) %s\n", old->hdr_cell[d_o]->data);
			d_o++;
			d_n++;
			break;
		case DIR_LUX:
			psd->mod_type = TAB_MOD_RENAME;
			psd->ref_idx_old = d_o;
			psd->ref_idx_new = d_n;
			//printf("%s -> %s\n", old->hdr_cell[d_o]->data, new->hdr_cell[d_n]->data);
			d_o++;
			d_n++;
			break;
		case DIR_U:
			psd->mod_type = TAB_MOD_REMOVE;
			psd->ref_idx_old = d_o;
			psd->ref_idx_new = -1;
			//printf("-) %s\n", old->hdr_cell[d_o]->data);
			d_o++;
			break;
		case DIR_L:
			psd->mod_type = TAB_MOD_INSERT;
			psd->ref_idx_old = -1;
			psd->ref_idx_new = d_n;
			//printf("+) %s\n", new->hdr_cell[d_n]->data);
			d_n++;
			break;
		}
	}

	assert(d_o == old->col && d_n == new->col);
	return si;
}

struct rowmod_hash{
	tab_diff_t			*psd;
	UT_hash_handle		hh;
	char				pk[0];
};


static int generate_diff_rows(const tab_desc_t *tab_old, const tab_desc_t *tab_new, primary_key_map_t **ppkm_old, primary_key_map_t **ppkm_new) {
	int step = 0;

	*ppkm_old = tab_build_primary_key_map(tab_old);
	*ppkm_new = tab_build_primary_key_map(tab_new);
	primary_key_t	**pkp_old = tab_get_primary_key_array(*ppkm_old);
	primary_key_t	**pkp_new = tab_get_primary_key_array(*ppkm_new);

	int n_edit = ld_path_of(pkp_old+TAB_DATA_ROW, tab_old->row-TAB_DATA_ROW, pkp_new+TAB_DATA_ROW, tab_new->row-TAB_DATA_ROW, pk_cmp, change_step, &step);

	int si = 0;
	for (int i = 0; i < TAB_DATA_ROW; i++) {
		tab_diff_t *psd = ad + si++;
		if (0 == pk_cmp(pkp_old[i], pkp_new[i]))
			psd->mod_type = TAB_MOD_NONE;
		else
			psd->mod_type = TAB_MOD_RENAME;
		psd->ref_idx_old = i;
		psd->ref_idx_new = i;
	}
	int d_o = TAB_DATA_ROW, d_n = TAB_DATA_ROW;
	struct rowmod_hash *_hash_head = NULL;
	for (int i = step - 1; i >= 0; i--, si++) {
		int dir = change_step[i];
		tab_diff_t *psd = ad + si;
		switch (dir) {
		case DIR_LU:
			psd->mod_type = TAB_MOD_NONE;
			psd->ref_idx_old = d_o;
			psd->ref_idx_new = d_n;
			d_o++;
			d_n++;
			break;
		case DIR_LUX:
			psd->mod_type = TAB_MOD_REMOVE;
			{
				struct rowmod_hash *res = NULL;
				const tab_cell_t *cell_d = pkp_old[d_o]->pkdata;
				HASH_FIND(hh, _hash_head, cell_d->data, cell_d->len, res);
				if (res) {
					assert(res->psd->mod_type == TAB_MOD_INSERT);
					res->psd->mod_type = TAB_MOD_SWAP_IN;	//SWAP IN
					//res->psd->ref_idx_new = res->psd->ref_idx_new;		//SWAP IN = d_n, d_o;
					res->psd->ref_idx_old = d_o;

					psd->mod_type = TAB_MOD_SWAP_OUT;		//SWAP OUT
					psd->ref_idx_old = -1;				//SWAP OUT = -1, -1;
					psd->ref_idx_new = -1;
				}
				else {
					res = tab_alloc(sizeof(struct rowmod_hash) + cell_d->len);
					memcpy(res->pk, cell_d->data, cell_d->len);
					res->psd = psd;
					HASH_ADD_KEYPTR(hh, _hash_head, res->pk, cell_d->len, res);
					psd->ref_idx_old = d_o;
					psd->ref_idx_new = -1;
				}
			}
			d_o++;

			psd = ad + ++si;
			psd->mod_type = TAB_MOD_INSERT;
			{
				struct rowmod_hash *res = NULL;
				const tab_cell_t *cell_d = pkp_new[d_n]->pkdata;
				HASH_FIND(hh, _hash_head, cell_d->data, cell_d->len, res);
				if (res) {
					assert(res->psd->mod_type == TAB_MOD_REMOVE);
					psd->mod_type = TAB_MOD_SWAP_OUT;
					psd->ref_idx_new = d_n;
					psd->ref_idx_old = res->psd->ref_idx_old;

					res->psd->mod_type = TAB_MOD_SWAP_IN;
					res->psd->ref_idx_new = -1;
					res->psd->ref_idx_old = -1;
				}
				else {
					res = tab_alloc(sizeof(struct rowmod_hash) + cell_d->len);
					memcpy(res->pk, cell_d->data, cell_d->len);
					res->psd = psd;
					HASH_ADD_KEYPTR(hh, _hash_head, res->pk, cell_d->len, res);
					psd->ref_idx_new = d_n;
					psd->ref_idx_old = -1;
				}
			}
			d_n++;
			break;

		case DIR_U:
			psd->mod_type = TAB_MOD_REMOVE;
			{
				struct rowmod_hash *res = NULL;
				const tab_cell_t *cell_d = pkp_old[d_o]->pkdata;
				HASH_FIND(hh, _hash_head, cell_d->data, cell_d->len, res);
				if (res) {
					assert(res->psd->mod_type == TAB_MOD_INSERT);
					res->psd->mod_type = TAB_MOD_SWAP_IN;	//SWAP IN
					//res->psd->ref_idx_new = res->psd->ref_idx_new;		//SWAP IN = d_n, d_o;
					res->psd->ref_idx_old = d_o;

					psd->mod_type = TAB_MOD_SWAP_OUT;		//SWAP OUT
					psd->ref_idx_old = -1;				//SWAP OUT = -1, -1;
					psd->ref_idx_new = -1;
				}
				else {
					res = tab_alloc(sizeof(struct rowmod_hash) + cell_d->len);
					memcpy(res->pk, cell_d->data, cell_d->len);
					res->psd = psd;
					HASH_ADD_KEYPTR(hh, _hash_head, res->pk, cell_d->len, res);
					psd->ref_idx_old = d_o;
					psd->ref_idx_new = -1;
				}
			}
			d_o++;
			break;
		case DIR_L:
			psd->mod_type = TAB_MOD_INSERT;
			{
				struct rowmod_hash *res = NULL;
				const tab_cell_t *cell_d = pkp_new[d_n]->pkdata;
				HASH_FIND(hh, _hash_head, cell_d->data, cell_d->len, res);
				if (res) {
					assert(res->psd->mod_type == TAB_MOD_REMOVE);
					psd->mod_type = TAB_MOD_SWAP_OUT;
					psd->ref_idx_new = d_n;
					psd->ref_idx_old = res->psd->ref_idx_old;

					res->psd->mod_type = TAB_MOD_SWAP_IN;
					res->psd->ref_idx_new = -1;
					res->psd->ref_idx_old = -1;
				}
				else {
					res = tab_alloc(sizeof(struct rowmod_hash) + cell_d->len);
					memcpy(res->pk, cell_d->data, cell_d->len);
					res->psd = psd;
					HASH_ADD_KEYPTR(hh, _hash_head, res->pk, cell_d->len, res);
					psd->ref_idx_new = d_n;
					psd->ref_idx_old = -1;
				}
			}
			d_n++;
			break;
		}
	}

//gc
	struct rowmod_hash *it, *tmp;
	HASH_ITER(hh, _hash_head, it, tmp) {
		HASH_DEL(_hash_head, it);
		tab_free(it);
	}

//filt out SWAP_OUTs
	int j = 0;
	for (int i = 0; i < si; i++) {
		tab_diff_t *psd = ad + i;
		if (!(psd->mod_type == TAB_MOD_SWAP_OUT && psd->ref_idx_new == -1 && psd->ref_idx_old == -1))
		{
			tab_diff_t *out = ad + j++;
			*out = *psd;
		}
	}
	assert(d_o == tab_old->row && d_n == tab_new->row);
	return j;
}

static void fix_diff(const tab_desc_t *tab_old, const tab_desc_t *tab_new, int *pncol, int *pnrow) {
	int ncol = *pncol;
	int nrow = *pnrow;

	int start = 2;
reentry1:
	for (int i = start; i < ncol; i++) {
		if (sd[i].mod_type == TAB_MOD_RENAME) {
			int ref_old = sd[i].ref_idx_old;
			int ref_new = sd[i].ref_idx_new;
			for (int j = 0; j < tab_old->row; j++) one_col_old[j] = tab_old->array[j][ref_old];
			for (int j = 0; j < tab_new->row; j++) one_col_new[j] = tab_new->array[j][ref_new];
			if (similarity_of(one_col_old, tab_old->row, one_col_new, tab_new->row, cell_cmp) < 0.8)
			{
				for (int j = ncol; j > i; j--)
					sd[j] = sd[j - 1];

				sd[i].mod_type = TAB_MOD_REMOVE;
				sd[i].ref_idx_new = -1;

				sd[i + 1].mod_type = TAB_MOD_INSERT;
				sd[i + 1].ref_idx_old = -1;

				ncol++;
				start = i+2;
				goto reentry1;
			}
		}
	}

	*pncol = ncol;
	*pnrow = nrow;
}

diff_desc_t *tab_diff_generate(const tab_desc_t *tab_old, const tab_desc_t *tab_new) {
	primary_key_map_t *pkm_old;
	primary_key_map_t *pkm_new;
	int ncol = generate_diff_cols(tab_old, tab_new);
	int nrow = generate_diff_rows(tab_old, tab_new, &pkm_old, &pkm_new);
	primary_key_t **pkp_old = tab_get_primary_key_array(pkm_old);
	primary_key_t **pkp_new = tab_get_primary_key_array(pkm_new);

	fix_diff(tab_old, tab_new, &ncol, &nrow);

	diff_desc_t *ret = (diff_desc_t *)tab_alloc(sizeof(diff_desc_t));
	ret->md_hdr = (struct col_mod *)tab_alloc(ncol * sizeof(struct col_mod));
	ret->md_array = (struct row_mod *)tab_alloc(sizeof(struct row_mod) * nrow);
	ret->ncols = ncol;
	ret->nrows = nrow;

	for (int i = 0; i < ncol; i++) {
		tab_diff_t *col_diff = sd + i;
		ret->md_hdr[i].mod_type = col_diff->mod_type;
		ret->md_hdr[i].cell_old = NULL;
		switch (col_diff->mod_type)  {
		case TAB_MOD_NONE:
			ret->md_hdr[i].cell = tab_new->hdr_cell[col_diff->ref_idx_new];
			break;
		case TAB_MOD_RENAME:
			ret->md_hdr[i].cell = tab_new->hdr_cell[col_diff->ref_idx_new];
			ret->md_hdr[i].cell_old = tab_old->hdr_cell[col_diff->ref_idx_old];
			break;
		case TAB_MOD_REMOVE:
			ret->md_hdr[i].cell = tab_old->hdr_cell[col_diff->ref_idx_old];
			break;
		case TAB_MOD_INSERT:
			ret->md_hdr[i].cell = tab_new->hdr_cell[col_diff->ref_idx_new];
			break;
		default:
			break;
		};
	}

	for (int j = 0; j < nrow; j++) {
		tab_diff_t *row_diff = ad + j;
		const primary_key_t *pko = row_diff->ref_idx_old >= 0 ? pkp_old[row_diff->ref_idx_old] : NULL;
		const primary_key_t *pkn = row_diff->ref_idx_new >= 0 ? pkp_new[row_diff->ref_idx_new] : NULL;

		const tab_cell_t **tab_old_row;
		const tab_cell_t **tab_new_row;
		if (j < 2) {
			tab_old_row = tab_old->array[j];
			tab_new_row = tab_new->array[j];
		} else {
			tab_old_row = pko ? find_row_by_pk(pkm_old, pko->pkdata) : NULL;
			tab_new_row = pkn ? find_row_by_pk(pkm_new, pkn->pkdata) : NULL;
		}

		struct row_mod *rm = &ret->md_array[j];
		rm->col = (struct col_mod *)tab_alloc(sizeof(struct col_mod) * ncol);
		rm->mod_type = row_diff->mod_type;
		switch (row_diff->mod_type) {
		case TAB_MOD_NONE:
			for (int i = 0; i < ncol; i++) {
				tab_diff_t *col_diff = sd + i;
				rm->col[i].mod_type = col_diff->mod_type;
				switch (col_diff->mod_type)  {
				case TAB_MOD_NONE:
				case TAB_MOD_RENAME:
					rm->col[i].cell = tab_new_row[col_diff->ref_idx_new];
					rm->col[i].cell_old = tab_old_row[col_diff->ref_idx_old];
					break;
				case TAB_MOD_REMOVE:
					rm->col[i].cell_old = tab_old_row[col_diff->ref_idx_old];
					rm->col[i].cell = NULL;
					break;
				case TAB_MOD_INSERT:
					rm->col[i].cell_old = NULL;
					rm->col[i].cell = tab_new_row[col_diff->ref_idx_new];
					break;
				};
			}
			break;
		case TAB_MOD_INSERT:
			for (int i = 0; i < ncol; i++) {
				tab_diff_t *col_diff = sd + i;
				rm->col[i].mod_type = col_diff->mod_type;
				rm->col[i].cell_old = NULL;
				switch (col_diff->mod_type)  {
				case TAB_MOD_NONE:
				case TAB_MOD_RENAME:
				case TAB_MOD_INSERT:
					rm->col[i].cell = tab_new_row[col_diff->ref_idx_new];
					break;
				case TAB_MOD_REMOVE:
					rm->col[i].cell = NULL;
					break;
				};
			}
			break;
		case TAB_MOD_REMOVE:
			for (int i = 0; i < ncol; i++) {
				tab_diff_t *col_diff = sd + i;
				rm->col[i].mod_type = col_diff->mod_type;
				rm->col[i].cell_old = col_diff->ref_idx_old >= 0 ? tab_old_row[col_diff->ref_idx_old] : NULL;
				switch (col_diff->mod_type)  {
				case TAB_MOD_NONE:
				case TAB_MOD_RENAME:
				case TAB_MOD_INSERT:
				case TAB_MOD_REMOVE:
					rm->col[i].cell = NULL;
					break;
				};
			}
			break;
		case TAB_MOD_RENAME: //only comment line or default line
			for (int i = 0; i < ncol; i++) {
				tab_diff_t *col_diff = sd + i;
				rm->col[i].mod_type = col_diff->mod_type;
				rm->col[i].cell_old = col_diff->ref_idx_old >= 0 ? tab_old_row[col_diff->ref_idx_old] : NULL;
				rm->col[i].cell = col_diff->ref_idx_new >= 0 ? tab_new_row[col_diff->ref_idx_new] : NULL;
			}
		case TAB_MOD_SWAP_IN:
			for (int i = 0; i < ncol; i++) {
				tab_diff_t *col_diff = sd + i;
				rm->col[i].mod_type = col_diff->mod_type;
				rm->col[i].cell_old = col_diff->ref_idx_old >= 0 ? tab_old_row[col_diff->ref_idx_old] : NULL;
				rm->col[i].cell = col_diff->ref_idx_new >= 0 ? tab_new_row[col_diff->ref_idx_new] : NULL;
			}
		default:
			break;
		}
	}
	tab_kill_primary_key_map(pkm_new);
	tab_kill_primary_key_map(pkm_old);
	return ret;
}

void tab_diff_del(diff_desc_t *dif)
{
	for (int i = 0; i < dif->nrows; ++i)
	{
		tab_free(dif->md_array[i].col);
	}
	tab_free(dif->md_hdr);
	tab_free(dif->md_array);
	tab_free(dif);
}

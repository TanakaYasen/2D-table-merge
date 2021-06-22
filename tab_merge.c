#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "ks/3rd/uthash.h"
#include "tab_log.h"

#include "core_algorithm.h"
#include "tab_data.h"
#include "tab_merge.h"

typedef struct tab_col_desc {
	const tab_cell_t *col_name;

	int ref_idx_parent;

	tab_modify_type_t ref_type_ours;
	int ref_idx_ours;
	const tab_cell_t *ref_name_ours;

	tab_modify_type_t ref_type_theirs;
	int ref_idx_theirs;
	const tab_cell_t *ref_name_theirs;
}tab_col_desc_t;

typedef struct tab_row_desc {
	const tab_cell_t		*pk_name;

	int ref_idx_parent;

	tab_modify_type_t		ref_type_ours;
	int						ref_idx_ours;
	const tab_cell_t		*ref_name_ours;

	tab_modify_type_t		ref_type_theirs;
	int						ref_idx_theirs;
	const tab_cell_t		*ref_name_theirs;
}tab_row_desc_t;

#define CONFLICT_FREE	0
#define CONFLICT_FALSE	1
#define CONFLICT_TRUE	2

#define TAB_INPUT_PARENT	0
#define TAB_INPUT_OURS		1 
#define TAB_INPUT_THEIRS	2

#define tab_alloc		malloc
#define tab_free		free
#define tab_realloc		realloc

#define MAX_SCHEMA_CHUNK	512
#define MAX_ACTION_CHUNK	MAX_ROW

#define MEMBEROF(_type, ptr, off) (*(_type *)((uint8_t *)(ptr) + off))
#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))

struct ts_hash_node {
	UT_hash_handle				hh;
	size_t						len;
	struct		tab_schema		*pts[2];
	char						data[0];
};

struct ta_hash_node {
	UT_hash_handle				hh;
	size_t						len;
	struct tab_action			*pta[2];
	char						data[0];
};

/*
typedef enum tab_modify_type {
	TAB_MOD_NONE,		//no change
	TAB_MOD_INSERT,		//插入
	TAB_MOD_RENAME,		//重命名
	TAB_MOD_SWAP_IN,	//换序
	TAB_MOD_SWAP_OUT,	//换序
	TAB_MOD_REMOVE,		//移除
}tab_modify_type_t;
*/
//-1=abort, 
static const int conflict_map[TAB_MOD_REMOVE + 1][TAB_MOD_REMOVE + 1] = {
	//none
	{ -1, -1, -1, -1, -1, -1 },
	//insert
	{ -1, 1, 2, -1, 0, -1 },
	//rename
	{ -1, 2, 2, 1, 0, -1 },
	//swap in
	{ -1, -1, 1, 1, 0, 1 },
	//swap out
	{ -1, 0, 0, 0, 0, 0 },
	//remove
	{ -1, -1, 2, 1, 0, 1},
};

static void __mark_schema_conflict(struct tab_schema*a, struct tab_schema*b)
{
	assert(a);
	if (NULL == b)
		return;

	switch (conflict_map[a->mt][b->mt])
	{
	case -1:
	{
		abort();
	}
	break;
	case 0:
	{
	}
	break;
	case 1:
	{
		a->conflict_with = b;
		b->conflict_with = a;
	}
	break;
	case 2:
	{
		//
	}
	default:
		break;
	}
}

static void __mark_action_conflict(struct tab_action*a, struct tab_action *b)
{
	assert(a);
	if (NULL == b)
		return;

	switch (conflict_map[a->mt][b->mt])
	{
	case -1:
	{
		abort();
	}
	break;
	case 0:
	{
	}
	break;
	case 1:
	{
		a->conflict_with = b;
		b->conflict_with = a;
	}
	break;
	case 2:
	{
		//
	}
	default:
		break;
	}
}

static const tab_modify_result_t conflict_priority[TAB_MOD_REMOVE + 1][TAB_MOD_REMOVE + 1][2] = {
	//none
	//TAB_RES_CONFLICT,		//无冲突
	//TAB_RES_TAKE_THIS,		//接受这个
	//TAB_RES_TAKE_OPPO,		//接受对面

	//TAB_MOD_NONE,	//no change
	//TAB_MOD_INSERT,	//插入
	//TAB_MOD_RENAME,	//重命名
	//TAB_MOD_SWAP,	//换序
	//TAB_MOD_REMOVE,	//移

	//none
	{
		{ TAB_RES_TAKE_OPPO, TAB_RES_TAKE_THIS },
		{ TAB_RES_TAKE_OPPO, TAB_RES_TAKE_THIS },
		{ TAB_RES_TAKE_OPPO, TAB_RES_TAKE_THIS },
		{ TAB_RES_TAKE_OPPO, TAB_RES_TAKE_THIS },
		{ TAB_RES_TAKE_OPPO, TAB_RES_TAKE_THIS },
	}, 
	//insert
	{
		{ TAB_RES_TAKE_THIS, TAB_RES_TAKE_OPPO },
		{ TAB_RES_CONFLICT, TAB_RES_CONFLICT },
		{ TAB_RES_CONFLICT, TAB_RES_CONFLICT },
		{ TAB_RES_IMPOSSIBLE, TAB_RES_IMPOSSIBLE },
		{ TAB_RES_IMPOSSIBLE, TAB_RES_IMPOSSIBLE },
	},
	//rename
	{
		{ TAB_RES_TAKE_THIS, TAB_RES_TAKE_OPPO },
		{ TAB_RES_CONFLICT, TAB_RES_CONFLICT }, 
		{ TAB_RES_CONFLICT, TAB_RES_CONFLICT },
		{ TAB_RES_CONFLICT, TAB_RES_CONFLICT },
		{ TAB_RES_TAKE_OPPO, TAB_RES_TAKE_THIS},
	},
	//swap
	{
		{ TAB_RES_TAKE_THIS, TAB_RES_TAKE_OPPO },
		{ TAB_RES_IMPOSSIBLE, TAB_RES_IMPOSSIBLE },
		{ TAB_RES_CONFLICT, TAB_RES_CONFLICT },
		{ TAB_RES_TAKE_THIS, TAB_RES_TAKE_OPPO },
		{ TAB_RES_TAKE_OPPO, TAB_RES_TAKE_THIS },
	},
	//remove
	{
		{ TAB_RES_TAKE_THIS, TAB_RES_TAKE_OPPO },
		{ TAB_RES_TAKE_THIS, TAB_RES_TAKE_OPPO },
		{ TAB_RES_TAKE_THIS, TAB_RES_TAKE_OPPO },
		{ TAB_RES_TAKE_THIS, TAB_RES_TAKE_OPPO },
		{ TAB_RES_TAKE_THIS, TAB_RES_TAKE_OPPO },
	},
};

typedef struct
{
	primary_key_map_t		*pkm_parent;
	primary_key_map_t		*pkm_ours;
	primary_key_map_t		*pkm_theirs;

	//our_contribution;
	//struct ts_hash_node		*lefts;
	//their_contribution;
	//struct ts_hash_node		*rights;

	struct tuple			*raw_tuple;

	struct tab_schema_chunk *schema_hunks;
	int						n_schemas;
	struct tab_action_chunk *action_hunks;
	int						n_actions;

	//produce 
	tab_col_desc_t	*final_cols;
	int				ncols;
	tab_row_desc_t	*final_rows;
	int				nrows;

	int				ncell_conflict;
}tab_merge_context_t;

static tab_merge_context_t *_make_merge_context(unsigned max_action)
{
	tab_merge_context_t *res = tab_alloc(sizeof(tab_merge_context_t));
	res->pkm_parent = NULL;
	res->pkm_ours = NULL;
	res->pkm_theirs = NULL;

	res->raw_tuple = tab_alloc(max_action * sizeof(struct tuple));

	res->final_cols = NULL;
	res->final_rows = NULL;
	res->action_hunks = NULL;
	res->n_actions = 0;
	res->schema_hunks = NULL;
	res->n_schemas = 0;
	return res;
}

static void _delete_merge_context(tab_merge_context_t *context)
{
#define SAFE_DELETE(x) do{if (x) {tab_free(x); x = NULL;}}while(0)
#define SAFE_KILL(x) do{if (x) {tab_kill_primary_key_map(x);}}while(0)
	//...
	SAFE_DELETE(context->raw_tuple);
	SAFE_DELETE(context->final_rows);
	SAFE_DELETE(context->final_cols);
	for (int i = 0; i < context->n_actions; i++)
	{
		struct tab_action_chunk *tac = &context->action_hunks[i];
		SAFE_DELETE(tac->action_left);
		SAFE_DELETE(tac->action_right);
	};

	for (int i = 0; i < context->n_schemas; i++)
	{
		struct tab_schema_chunk *tsc = &context->schema_hunks[i];
		SAFE_DELETE(tsc->schema_left);
		SAFE_DELETE(tsc->schema_right);
	};
	SAFE_DELETE(context->final_rows);
	SAFE_DELETE(context->final_rows);

	SAFE_KILL(context->pkm_parent);
	SAFE_KILL(context->pkm_ours);
	SAFE_KILL(context->pkm_theirs);

	tab_free(context);
#undef SAFE_KILL
#undef SAFE_DELETE
}


static int _linar_search_col(const tab_desc_t *tab, tab_cell_t *col_name) {
	for (int i = 0; i < tab->col; i++) {
		if (0 == cell_cmp(tab->hdr_cell[i], col_name)) {
			return i;
		}
	}
	return -1;
}

static int _linar_search_pk(const primary_key_t **pk_arr, int narr, tab_cell_t *pkname) {
	for (int i = 0; i < narr; i++) {
		if (0 == cell_cmp(pk_arr[i]->pkdata, pkname)) {
			return i;
		}
	}
	return -1;
}

static double similarity_of_two_cols(const tab_desc_t *left, int col_left, const tab_desc_t *right, int col_right) {
	double	res = 0.0;
	int row_left = left->row;
	int row_right = right->row;
	const tab_cell_t **col_arr_left = tab_alloc(sizeof(void*)*(left->row + 2));
	const tab_cell_t **col_arr_right = tab_alloc(sizeof(void*)*(right->row + 2));

	for (int i = 0; i < row_left; i++) {
		col_arr_left[i] = left->array[i][col_left];
	}
	for (int i = 0; i < row_right; i++) {
		col_arr_right[i] = right->array[i][col_right];
	}
	res = similarity_of((void **)col_arr_left, row_left, (void **)col_arr_right, row_right, cell_cmp);
	tab_free((void *)col_arr_right);
	tab_free((void *)col_arr_left);
	return res;
}

static int check_change(void **a, const struct pair *rangeA, void **b, const struct pair *rangeB, f_cmp pcmp) {
	if (rangeA->to - rangeA->from != rangeB->to - rangeB->from)
		return 1;
	int r = rangeA->to - rangeA->from;
	for (int i = 0; i <= r; i++) {
		if (pcmp(a[i + rangeA->from], b[i + rangeB->from]) != 0)
			return 1;;
	}
	return 0;
}

static struct ts_hash_node *schema_do_self_adjust(struct tab_schema_chunk *vec, int sn, ptrdiff_t off_nsize, ptrdiff_t off_ptr) {
	struct ts_hash_node *tsh = NULL;

	for (int i = 0; i < sn; i++) {
		struct tab_schema_chunk *tsc = &vec[i];
		int nc = MEMBEROF(int, tsc, off_nsize);
		for (int j = 0; j < nc; j++) {
			struct tab_schema *cur = &(MEMBEROF(struct tab_schema *, tsc, off_ptr)[j]);
			struct ts_hash_node *hn = NULL;
			if (cur->mt == TAB_MOD_RENAME)
			{
				tab_cell_t *oldnew[2] = { cur->cell_old, cur->cell_new };
				for (int k = 0; k < sizeof(oldnew) / sizeof(oldnew[0]); k++)
				{
					HASH_FIND(hh, tsh, cur->cell_old->data, cur->cell_old->len, hn);
					if (NULL == hn) {
						hn = tab_alloc(sizeof(struct ts_hash_node) + cur->cell_old->len + 1);
						hn->pts[0] = hn->pts[1] = NULL;
						hn->len = cur->pcell->len;
						hn->data[hn->len] = '\0';
						memcpy(hn->data, cur->pcell->data, hn->len);
						(hn->pts[0] = cur);
						HASH_ADD_KEYPTR(hh, tsh, hn->data, hn->len, hn);
					}
				}
				continue;
			}

			HASH_FIND(hh, tsh, cur->pcell->data, cur->pcell->len, hn);
			if (NULL == hn) {
				hn = tab_alloc(sizeof(struct ts_hash_node) + cur->pcell->len + 1);
				hn->pts[0] = hn->pts[1] = NULL;
				hn->len = cur->pcell->len;
				hn->data[hn->len] = '\0';
				memcpy(hn->data, cur->pcell->data, hn->len);
				(hn->pts[0] = cur);
				HASH_ADD_KEYPTR(hh, tsh, hn->data, hn->len, hn);
			}
			else {
				struct tab_schema *prev = hn->pts[0];
				assert(prev);
				if (cur->mt == TAB_MOD_INSERT) {
					assert(prev->mt == TAB_MOD_REMOVE);
					prev->mt = TAB_MOD_SWAP_OUT;
					cur->mt = TAB_MOD_SWAP_IN;
					prev->ref_idx = -1;
					hn->pts[1] = cur;
				}
				else if (cur->mt == TAB_MOD_REMOVE) {
					assert(prev->mt == TAB_MOD_INSERT);
					prev->mt = TAB_MOD_SWAP_IN;
					cur->mt = TAB_MOD_SWAP_OUT;
					prev->ref_idx = -1;
					hn->pts[1] = cur;
				}
			}
		}
	}
	return tsh;
}

static void schema_do_figure_conflicts(struct ts_hash_node *lefts,	struct ts_hash_node *rights)
{
	struct ts_hash_node *tmp, *cur;
	struct tab_schema *tsl, *tsr;
	HASH_ITER(hh, lefts, cur, tmp) {
		for (int i = 0; i < 2; i++)
		{
			tsl = cur->pts[i];
			if (tsl)
			{
				struct ts_hash_node *other = NULL;
				HASH_FIND(hh, rights, cur->data, cur->len, other);
				if (other) {
					tsr = other->pts[0];
					__mark_schema_conflict(tsl, tsr);
					tsr = other->pts[1];
					__mark_schema_conflict(tsl, tsr);
				}
			}
		}
	};
	HASH_ITER(hh, rights, cur, tmp) {
		for (int i = 0; i < 2; i++)
		{
			tsr = cur->pts[i];
			if (tsr)
			{
				struct ts_hash_node *other = NULL;
				HASH_FIND(hh, lefts, cur->data, cur->len, other);
				if (other) {
					tsl = other->pts[0];
					__mark_schema_conflict(tsr, tsl);
					tsl = other->pts[1];
					__mark_schema_conflict(tsr, tsl);
				}
			}
		}
	};

	HASH_ITER(hh, lefts, cur, tmp) {
		HASH_DEL(lefts, cur);
		tab_free(cur);
	};
	HASH_ITER(hh, rights, cur, tmp) {
		HASH_DEL(rights, cur);
		tab_free(cur);
	};

}

static struct ta_hash_node *action_do_self_adjust(struct tab_action_chunk *vec, int sn, ptrdiff_t off_nsize, ptrdiff_t off_ptr) {
	struct ta_hash_node *tsh = NULL;
	for (int i = 0; i < sn; i++) {
		struct tab_action_chunk *tac = &vec[i];
		int nc = MEMBEROF(int, tac, off_nsize);
		for (int j = 0; j < nc; j++) {
			struct ta_hash_node *hn = NULL;
			struct tab_action *cur = &(MEMBEROF(struct tab_action *, tac, off_ptr)[j]);
			HASH_FIND(hh, tsh, cur->pcell->data, cur->pcell->len, hn);
			if (NULL == hn) {
				hn = tab_alloc(sizeof(struct ta_hash_node) + cur->cell_insert->len + 1);
				hn->pta[0] = hn->pta[1] = NULL;
				hn->len = cur->cell_insert->len;
				hn->data[hn->len] = '\0';
				memcpy(hn->data, cur->cell_insert->data, hn->len);
				hn->pta[0] = cur;
				HASH_ADD_KEYPTR(hh, tsh, hn->data, hn->len, hn);
			}
			else {
				struct tab_action *prev = hn->pta[0];
				if (cur->mt == TAB_MOD_INSERT) {
					if (prev->mt == TAB_MOD_REMOVE) {
						cur->mt = TAB_MOD_SWAP_IN;
						prev->mt = TAB_MOD_SWAP_OUT;
						prev->ref_idx = -1;
					}
					else if (prev->mt == TAB_MOD_INSERT) {
						return NULL;
					} else if (prev->mt == TAB_MOD_RENAME || prev->mt ==  TAB_MOD_INSERT) {
						abort();
					}
				}
				else if (cur->mt == TAB_MOD_REMOVE) {
					if (prev->mt == TAB_MOD_INSERT) {
						cur->mt = TAB_MOD_SWAP_OUT;
						prev->mt = TAB_MOD_SWAP_IN;
						cur->ref_idx = -1;
					}
					else if (prev->mt == TAB_MOD_INSERT) {
						return NULL;
					} else if (prev->mt == TAB_MOD_RENAME || prev->mt ==  TAB_MOD_INSERT) {
						abort();
					}
				}
				hn->pta[1] = cur;
			}
		}
	}
	return tsh;
}

static int __merge_collect_delta_schemas(struct tab_schema_chunk *tsc, int chunk_idx, struct tab_schema **s, int *n
		, const tab_desc_t *tab_parent, const struct pair *span_parent
		, const tab_desc_t *tab_mod, const struct pair *span_mod)
{
	struct tab_schema tmp;
	tmp.phunk = tsc;
	tmp.conflict_with = NULL;
	tmp.res = TAB_RES_UNDICIDED;
	int capacity = (span_mod->to - span_mod->from + 1) + (span_parent->to - span_parent->from + 1);

	struct tab_schema *ts = (struct tab_schema *)tab_alloc(sizeof(struct tab_schema) * capacity);
	int	k = 0;

	for (int i = span_mod->from; i <= span_mod->to; i++)
	{
		int found = 0;
		int jj = -1;
		for (int j = span_parent->from; j <= span_parent->to; j++)
		{
			if (0 == cell_cmp(tab_mod->hdr_cell[i], tab_parent->hdr_cell[j]))
			{
				found = 1;
				jj = j;
				break;
			}
		}
		if (!found)
		{
			//insert
			tmp.mt = TAB_MOD_INSERT;
			tmp.cell_insert = tab_mod->hdr_cell[i];
			tmp.ref_idx = i;
			ts[k++] = tmp;
		}
		else
		{
			tab_log_warn("none");
			tmp.mt = TAB_MOD_NONE;
			tmp.pcell = tab_parent->hdr_cell[jj];
			tmp.ref_idx = jj;
			ts[k++] = tmp;
		}
	}
	for (int j = span_parent->from; j <= span_parent->to; j++)
	{
		int found = 0;
		for (int i = span_mod->from; i <= span_mod->to; i++)
		{
			if (0 == cell_cmp(tab_parent->hdr_cell[j], tab_mod->hdr_cell[i]))
			{
				found = 1;
				break;
			}
		}
		if (!found)
		{
			//remove
			tmp.mt = TAB_MOD_REMOVE;
			tmp.cell_remove = tab_parent->hdr_cell[j];
			tmp.ref_idx = j;
			ts[k++] = tmp;
		}
	}
	if (k == 2)
	{
		if (ts[0].mt == TAB_MOD_INSERT && ts[1].mt == TAB_MOD_REMOVE)
		{
			double sim = similarity_of_two_cols(tab_parent, ts[1].ref_idx, tab_mod, ts[0].ref_idx);
			if (sim > 0.9)
			{
				k = 1;
				tmp.mt = TAB_MOD_RENAME;
				tmp.cell_old = tab_parent->hdr_cell[
														ts[1].ref_idx	];
				tmp.cell_new = tab_mod->hdr_cell[
														ts[0].ref_idx	];
				ts[0] = tmp;
			}
		}
	}
	*s = ts;
	*n = k;

	return k;
}

static void action_do_figure_conflicts(struct ta_hash_node *lefts, struct ta_hash_node *rights)
{
	struct ta_hash_node *tmp, *cur;
	struct tab_action *tsl, *tsr;
	HASH_ITER(hh, lefts, cur, tmp) {
		for (int i = 0; i < 2; i++)
		{
			tsl = cur->pta[i];
			if (tsl)
			{
				struct ta_hash_node *other = NULL;
				HASH_FIND(hh, rights, cur->data, cur->len, other);
				if (other) {
					tsr = other->pta[0];
					__mark_action_conflict(tsl, tsr);
					tsr = other->pta[1];
					__mark_action_conflict(tsl, tsr);
				}
			}
		}
	};
	HASH_ITER(hh, rights, cur, tmp) {
		for (int i = 0; i < 2; i++)
		{
			tsr = cur->pta[i];
			if (tsr)
			{
				struct ta_hash_node *other = NULL;
				HASH_FIND(hh, lefts, cur->data, cur->len, other);
				if (other) {
					tsl = other->pta[0];
					__mark_action_conflict(tsr, tsl);
					tsl = other->pta[1];
					__mark_action_conflict(tsr, tsl);
				}
			}
		}
	};

	HASH_ITER(hh, lefts, cur, tmp) {
		HASH_DEL(lefts, cur);
		tab_free(cur);
	};
	HASH_ITER(hh, rights, cur, tmp) {
		HASH_DEL(rights, cur);
		tab_free(cur);
	};
}

static int generate_schema_chunks(tab_merge_context_t *mcontext, const tab_desc_t *tab_parent, const tab_desc_t *tab_ours, const tab_desc_t *tab_theirs) 
{
	struct tuple *raw_tuple = mcontext->raw_tuple;
	struct ts_hash_node *lefts = NULL;
	struct ts_hash_node *rights = NULL;

	int nchunk = match_max_chunks(tab_parent->hdr_cell, tab_parent->col
		, tab_ours->hdr_cell, tab_ours->col
		, tab_theirs->hdr_cell, tab_theirs->col
		, cell_cmp, raw_tuple);

	struct tab_schema_chunk *schema_hunks = tab_alloc(sizeof(struct tab_schema_chunk) * nchunk);
	memset(schema_hunks, 0, sizeof(struct tab_schema_chunk) * nchunk);
	assert(NULL == mcontext->schema_hunks);
	mcontext->schema_hunks = schema_hunks;

	for (int s = 0; s < nchunk; s++) {
		struct tuple *tp = &raw_tuple[s];
		struct tab_schema_chunk *pchunk = &schema_hunks[s];
		struct tab_schema_chunk tmp = { *tp, 0, 0,0,0, 0, NULL, NULL};
		if (tp->stable) {
			assert(tp->a.from - tp->a.to == tp->o.from - tp->o.to);
			assert(tp->b.from - tp->b.to == tp->o.from - tp->o.to);
			*pchunk = tmp;
		}
		else {
			const struct pair *a = &tp->a, *o = &tp->o, *b = &tp->b;
			int conflict = 0;
			int changeA = check_change(tab_parent->hdr_cell, o, tab_ours->hdr_cell, a, cell_cmp);
			int changeB = check_change(tab_parent->hdr_cell, o, tab_theirs->hdr_cell, b, cell_cmp);
			if (changeA && changeB) {
				conflict = check_change(tab_ours->hdr_cell, a, tab_theirs->hdr_cell, b, cell_cmp);
			}
			pchunk->change_left = changeA;
			pchunk->change_right = changeB;
			pchunk->conflict = conflict;
			__merge_collect_delta_schemas(pchunk, s, &pchunk->schema_left, &pchunk->n_left, tab_parent, o, tab_ours, a);
			__merge_collect_delta_schemas(pchunk, s, &pchunk->schema_right, &pchunk->n_right, tab_parent, o, tab_theirs, b);
		}
	}

	lefts = schema_do_self_adjust(schema_hunks, nchunk, OFFSETOF(struct tab_schema_chunk, n_left), OFFSETOF(struct tab_schema_chunk, schema_left));
	rights = schema_do_self_adjust(schema_hunks, nchunk, OFFSETOF(struct tab_schema_chunk, n_right), OFFSETOF(struct tab_schema_chunk, schema_right));

	schema_do_figure_conflicts(lefts, rights);
	//schema_do_report_changes()
	return nchunk;
}


static void debug_report_schema(const struct tab_schema *ts, int locate, const char *side)
{
	switch (ts->mt)
	{
	case TAB_MOD_NONE:
		break;
	case TAB_MOD_INSERT:
		tab_log_debug("%s#%d: +col)%s\n", side, locate, ts->cell_insert->data);
		break;
	case TAB_MOD_REMOVE:
		tab_log_debug("%s#%d: -col)%s\n", side, locate, ts->cell_remove->data);
		break;
	case TAB_MOD_RENAME:
		tab_log_debug("%s#%d: *col)%s->%s\n", side, locate, ts->cell_old->data, ts->cell_new->data);
		break;
	case TAB_MOD_SWAP_IN:
		tab_log_debug("%s#%d: =col)%s\n", side, locate, ts->cell_swap->data);
		break;
	}

}
static int solve_schemas_conflict(struct tab_schema_chunk *tsc, int n) {
	for (int i = 0; i < n; i++) {
		struct tab_schema_chunk *ts = &tsc[i];
		if (ts->tp.stable)
			continue;

		for (int j = 0; j < ts->n_left; j++) {
			struct tab_schema *s = &ts->schema_left[j];
			debug_report_schema(s, i, "LEFT");
			if (s->res != TAB_RES_UNDICIDED)
				continue;
			if (!s->conflict_with) {
				s->res = TAB_RES_TAKE_THIS;
			}
			else {
				struct tab_schema *oppo = s->conflict_with;
				s->res = conflict_priority[s->mt][oppo->mt][0];
				oppo->res = conflict_priority[s->mt][oppo->mt][1];
			}
		}
		for (int j = 0; j < ts->n_right; j++) {
			struct tab_schema *s = &ts->schema_right[j];
			debug_report_schema(s, i, "RIGHT");
			if (s->res != TAB_RES_UNDICIDED)
				continue;
			if (!s->conflict_with) {
				s->res = TAB_RES_TAKE_THIS;
			}
			else {
				struct tab_schema *oppo = s->conflict_with;
				s->res = conflict_priority[s->mt][oppo->mt][0];
				oppo->res = conflict_priority[s->mt][oppo->mt][1];
			}
		}
	}

	for (int i = 0; i < n; i++) {
		struct tab_schema_chunk *ts = &tsc[i];
		if (ts->tp.stable)
			continue;
		for (int j = 0; j < ts->n_left; j++) {
			struct tab_schema *s = &ts->schema_left[j];
			if (s->res == TAB_RES_CONFLICT)
				return 1;
		}
		for (int j = 0; j < ts->n_right; j++) {
			struct tab_schema *s = &ts->schema_right[j];
			if (s->res == TAB_RES_CONFLICT)
				return 1;
		}
	}
	return 0;
}

//scan schemas
//this coroutine scans the schemas and find self-conflicts or rearrangement.
static void sub_merge_schema(tab_col_desc_t **pcursor, struct tab_schema *tss, int n, 
	const tab_desc_t *tab_parent, const tab_desc_t *tab_oppo,
	ptrdiff_t off_rn_this,
	ptrdiff_t off_ri_this,
	ptrdiff_t off_rt_this,
	ptrdiff_t off_rn_oppo,
	ptrdiff_t off_ri_oppo,
	ptrdiff_t off_rt_oppo,
	const char *desc) {
	tab_col_desc_t *cursor = *pcursor;

	if (tss) {
		for (int j = 0; j < n; j++) {
			do {
				cursor->col_name = cursor->ref_name_ours = cursor->ref_name_theirs = NULL;
				cursor->ref_idx_ours = cursor->ref_idx_theirs = cursor->ref_idx_parent = -1;
				cursor->ref_type_ours = cursor->ref_type_theirs = TAB_MOD_NONE;
			} while (0);

			struct tab_schema *ts = &tss[j];
			if (ts->res == TAB_RES_TAKE_THIS) {
				if (ts->mt == TAB_MOD_INSERT) {
					cursor->col_name = ts->cell_insert;
					MEMBEROF(const tab_cell_t *, cursor, off_rn_this) = cursor->col_name;
					MEMBEROF(tab_modify_type_t, cursor, off_rt_this) = TAB_MOD_INSERT;
					MEMBEROF(int, cursor, off_ri_this) = ts->ref_idx;

					cursor++;
				}
				else if (ts->mt == TAB_MOD_SWAP_IN)
				{
					cursor->col_name = ts->cell_swap;

					cursor->ref_idx_parent = _linar_search_col(tab_parent, ts->cell_swap);

					MEMBEROF(const tab_cell_t *, cursor, off_rn_this) = cursor->col_name;
					MEMBEROF(tab_modify_type_t, cursor, off_rt_this) = TAB_MOD_SWAP_IN;
					MEMBEROF(int, cursor, off_ri_this) = ts->ref_idx;

					MEMBEROF(const tab_cell_t *, cursor, off_rn_oppo) = ts->cell_swap;
					MEMBEROF(tab_modify_type_t, cursor, off_rt_oppo) = TAB_MOD_NONE;
					MEMBEROF(int, cursor, off_ri_oppo) = _linar_search_col(tab_oppo, ts->cell_swap);

					cursor++;
				}
				else if (ts->mt == TAB_MOD_RENAME) {
					cursor->col_name = ts->cell_new;

					cursor->ref_idx_parent = _linar_search_col(tab_parent, ts->cell_old);

					MEMBEROF(const tab_cell_t *, cursor, off_rn_this) = cursor->col_name;
					MEMBEROF(tab_modify_type_t, cursor, off_rt_this) = TAB_MOD_RENAME;
					MEMBEROF(int, cursor, off_ri_this) = ts->ref_idx;

					MEMBEROF(const tab_cell_t *, cursor, off_rn_oppo) = ts->cell_swap;
					MEMBEROF(tab_modify_type_t, cursor, off_rt_oppo) = TAB_MOD_NONE;
					MEMBEROF(int, cursor, off_ri_oppo) = _linar_search_col(tab_oppo, ts->cell_swap);

					cursor++;
				}
			}
		}
	}
	*pcursor = cursor;
}

static void sub_merge_action(tab_row_desc_t **pcursor, struct tab_action *tss, int n, 
	const primary_key_t **pkarr_oppo, int noppo,
	ptrdiff_t off_rn_this,
	ptrdiff_t off_ri_this,
	ptrdiff_t off_rt_this,
	ptrdiff_t off_rn_oppo,
	ptrdiff_t off_ri_oppo,
	ptrdiff_t off_rt_oppo,
	const char *desc) 
{
	tab_row_desc_t *cursor = *pcursor;
	if (tss) {
		for (int j = 0; j < n; j++) {
			struct tab_action *ts = &tss[j];
			do {
				cursor->pk_name = cursor->ref_name_ours = cursor->ref_name_theirs = NULL;
				cursor->ref_idx_ours = cursor->ref_idx_theirs = cursor->ref_idx_parent = -1;
				cursor->ref_type_ours = cursor->ref_type_theirs = TAB_MOD_NONE;
			} while (0);
			if (ts->res == TAB_RES_TAKE_THIS) {
				if (ts->mt == TAB_MOD_INSERT) {
					cursor->pk_name = ts->cell_insert;
					MEMBEROF(const tab_cell_t *, cursor, off_rn_this) = cursor->pk_name;
					MEMBEROF(tab_modify_type_t, cursor, off_rt_this) = TAB_MOD_INSERT;
					MEMBEROF(int, cursor, off_ri_this) = ts->ref_idx;

					cursor++;
				}
				else if (ts->mt == TAB_MOD_SWAP_IN)
				{
					cursor->pk_name = ts->cell_swap;
					MEMBEROF(const tab_cell_t *, cursor, off_rn_this) = cursor->pk_name;
					MEMBEROF(tab_modify_type_t, cursor, off_rt_this) = TAB_MOD_SWAP_IN;
					MEMBEROF(int, cursor, off_ri_this) = ts->ref_idx;

					MEMBEROF(const tab_cell_t *, cursor, off_rn_oppo) = ts->cell_swap;
					MEMBEROF(tab_modify_type_t, cursor, off_rt_oppo) = TAB_MOD_NONE;
					MEMBEROF(int, cursor, off_ri_oppo) = _linar_search_pk(pkarr_oppo, noppo, ts->cell_swap);

					cursor++;
				}
				else if (ts->mt == TAB_MOD_NONE) {
					cursor->pk_name = ts->pcell;
					MEMBEROF(const tab_cell_t *, cursor, off_rn_this) = cursor->pk_name;
					MEMBEROF(tab_modify_type_t, cursor, off_rt_this) = TAB_MOD_NONE;
					MEMBEROF(int, cursor, off_ri_this) = ts->ref_idx;

					cursor++;
				}
			}
		}
	}
	*pcursor = cursor;
}

static int merge_schemas( tab_merge_context_t *mcontext
	, const tab_desc_t *tab_parent, const tab_desc_t *tab_ours, const tab_desc_t *tab_theirs
	, struct tab_schema_chunk *tsc, int n)
{
	tab_col_desc_t *cursor;
	const int init_size = sizeof(tab_col_desc_t) * (tab_theirs->col + tab_ours->col);
	mcontext->final_cols = cursor = (tab_col_desc_t *)tab_alloc(init_size);// output;

	for (int i = 0; i < n; i++) {
		struct tab_schema_chunk *ts_cur = &tsc[i];
		if (ts_cur->tp.stable) {
			for (int j = 0; j <= ts_cur->tp.o.to - ts_cur->tp.o.from; j++) {
				cursor->col_name = tab_parent->hdr_cell[ts_cur->tp.o.from + j];
				
				cursor->ref_idx_parent = ts_cur->tp.o.from + j;

				cursor->ref_name_ours = tab_ours->hdr_cell[ts_cur->tp.a.from + j];
				cursor->ref_idx_ours = ts_cur->tp.a.from + j;
				cursor->ref_type_ours = TAB_MOD_NONE;

				cursor->ref_name_theirs = tab_theirs->hdr_cell[ts_cur->tp.b.from + j];
				cursor->ref_idx_theirs = ts_cur->tp.b.from + j;
				cursor->ref_type_theirs = TAB_MOD_NONE;

				cursor++;
			}
		}
		else {
			sub_merge_schema(&cursor, ts_cur->schema_left, ts_cur->n_left,
				tab_parent, tab_theirs, OFFSETOF(tab_col_desc_t, ref_name_ours), OFFSETOF(tab_col_desc_t, ref_idx_ours), OFFSETOF(tab_col_desc_t, ref_type_ours),
				OFFSETOF(tab_col_desc_t, ref_name_theirs), OFFSETOF(tab_col_desc_t, ref_idx_theirs), OFFSETOF(tab_col_desc_t, ref_type_theirs),
				"ours");
			sub_merge_schema(&cursor, ts_cur->schema_right, ts_cur->n_right,
				tab_parent, tab_ours, OFFSETOF(tab_col_desc_t, ref_name_theirs), OFFSETOF(tab_col_desc_t, ref_idx_theirs), OFFSETOF(tab_col_desc_t, ref_type_theirs),
				OFFSETOF(tab_col_desc_t, ref_name_ours), OFFSETOF(tab_col_desc_t, ref_idx_ours), OFFSETOF(tab_col_desc_t, ref_type_ours),
				"theirs");
		}
	}

	mcontext->ncols = (int)(cursor - mcontext->final_cols);
	return mcontext->ncols;
}

static int __merge_collect_delta_actions(struct tab_action_chunk *tsc, int chunk_idx, struct tab_action **s, int *n
	, const primary_key_t **pkarr_parent, const struct pair *span_parent
	, const primary_key_t **pkarr_mod, const struct pair *span_mod)
{
	struct tab_action tmp;
	tmp.phunk = tsc;
	//tmp.ck_idx = chunk_idx;
	tmp.conflict_with = NULL;
	tmp.res = TAB_RES_UNDICIDED;
	int capacity = (span_mod->to - span_mod->from + 1) + (span_parent->to - span_parent->from + 1);

	struct tab_action *ta = (struct tab_action *)tab_alloc(sizeof(struct tab_action) * capacity);
	int	k = 0;

	for (int i = span_mod->from; i <= span_mod->to; i++)
	{
		int found = 0;
		int jj = -1;
		for (int j = span_parent->from; j <= span_parent->to; j++)
		{
			if (0 == pk_cmp(pkarr_mod[i], pkarr_parent[j]))
			{
				found = 1;
				jj = j;
				break;
			}
		}
		if (!found)
		{
			//insert
			tmp.mt = TAB_MOD_INSERT;
			tmp.cell_insert = pkarr_mod[i]->pkdata;
			tmp.ref_idx = i;
			ta[k++] = tmp;
		}
		else
		{
			tmp.mt = TAB_MOD_NONE;
			tmp.pcell = pkarr_parent[jj]->pkdata;
			tmp.ref_idx = jj;
		}
	}
	for (int j = span_parent->from; j <= span_parent->to; j++)
	{
		int found = 0;
		for (int i = span_mod->from; i <= span_mod->to; i++)
		{
			if (0 == pk_cmp(pkarr_mod[i], pkarr_parent[j]))
			{
				found = 1;
				break;
			}
		}
		if (!found)
		{
			//remove
			tmp.mt = TAB_MOD_REMOVE;
			tmp.cell_remove = pkarr_parent[j]->pkdata;
			tmp.ref_idx = j;
			ta[k++] = tmp;
		}
	}
	*s = ta;
	*n = k;
	return k;
}


static int generate_action_chunks(
	tab_merge_context_t	*mcontext,
	const tab_desc_t *tab_parent,
	const tab_desc_t *tab_ours,
	const tab_desc_t *tab_theirs)
{
	struct tuple *raw_tuple = mcontext->raw_tuple;

	//merge_context
	mcontext->pkm_parent = tab_build_primary_key_map(tab_parent);
	mcontext->pkm_ours = tab_build_primary_key_map(tab_ours);
	mcontext->pkm_theirs = tab_build_primary_key_map(tab_theirs);

	primary_key_t **pkp_parent = tab_get_primary_key_array(mcontext->pkm_parent);
	primary_key_t **pkp_ours = tab_get_primary_key_array(mcontext->pkm_ours);
	primary_key_t **pkp_theirs = tab_get_primary_key_array(mcontext->pkm_theirs);

	int nchunk = match_max_chunks(pkp_parent, tab_parent->row
		, pkp_ours, tab_ours->row
		, pkp_theirs, tab_theirs->row
		, pk_cmp, raw_tuple);

	struct ta_hash_node *lefts = NULL;
	struct ta_hash_node *rights = NULL;

	struct tab_action_chunk *action_hunks = tab_alloc(sizeof(struct tab_action_chunk) * nchunk);
	memset(action_hunks, 0, sizeof(struct tab_action_chunk) * nchunk);
	mcontext->action_hunks = action_hunks;

	for (int s = 0; s < nchunk; s++) {
		struct tuple *tp = &raw_tuple[s];
		struct tab_action_chunk *pchunk = &action_hunks[s];
		struct tab_action_chunk tmp = { *tp, 0, 0, NULL, NULL };
		if (tp->stable) {
			assert(tp->a.from - tp->a.to == tp->o.from - tp->o.to);
			assert(tp->b.from - tp->b.to == tp->o.from - tp->o.to);
			*pchunk = tmp;
		}
		else {
			const struct pair *a = &tp->a, *o = &tp->o, *b = &tp->b;
			int changeA = 0, changeB = 0;
			int conflict = 0;
			changeA = check_change(pkp_parent, o, pkp_ours, a, pk_cmp);
			changeB = check_change(pkp_parent, o, pkp_theirs, b, pk_cmp);
			if (changeA && changeB) {
				conflict = check_change(pkp_ours, a, pkp_theirs, b, pk_cmp);
			}
			__merge_collect_delta_actions(pchunk, s, &pchunk->action_left, &pchunk->n_left, pkp_parent, o, pkp_ours, a);
			__merge_collect_delta_actions(pchunk, s, &pchunk->action_right, &pchunk->n_right, pkp_parent, o, pkp_theirs, b);
		}
	}

	lefts = action_do_self_adjust(action_hunks, nchunk, OFFSETOF(struct tab_action_chunk, n_left),  OFFSETOF(struct tab_action_chunk, action_left));
	rights = action_do_self_adjust(action_hunks, nchunk, OFFSETOF(struct tab_action_chunk, n_right),  OFFSETOF(struct tab_action_chunk, action_right));

	action_do_figure_conflicts(lefts, rights);
	return nchunk;
}

static void debug_report_action(const struct tab_action *ta, int locate, const char *side)
{
	switch (ta->mt)
	{
	case TAB_MOD_NONE:
		break;
	case TAB_MOD_INSERT:
		tab_log_debug("%s#%d +row) %s\n", side, locate, sharp_split_whole_pk(ta->cell_insert));
		break;
	case TAB_MOD_REMOVE:
		tab_log_debug("%s#%d -row) %s\n", side, locate, sharp_split_whole_pk(ta->cell_remove));
		break;
	case TAB_MOD_SWAP_IN:
		tab_log_debug("%s#%d =row) %s\n", side, locate, sharp_split_whole_pk(ta->cell_swap));
		break;
	}
}

static int solve_actions_conflict(struct tab_action_chunk *tsc, int n, const tab_desc_t *tab_ours, const tab_desc_t *tab_theirs, int n_cols, tab_col_desc_t final_cols[]) {
	for (int i = 0; i < n; i++) {
		struct tab_action_chunk *ts = &tsc[i];
		for (int j = 0; j < ts->n_left; j++) {
			struct tab_action *s = &ts->action_left[j];
			debug_report_action(s, i, "LEFT");
			if (s->res != TAB_RES_UNDICIDED)
				continue;
			if (!s->conflict_with) {
				s->res = TAB_RES_TAKE_THIS;
			}
			else {
				struct tab_action *oppo = s->conflict_with;
				int conf_flag = 1;
				if (s->mt == TAB_MOD_INSERT && oppo->mt == TAB_MOD_INSERT)
				{
					conf_flag = 0;
					const tab_cell_t **row_ours = tab_ours->array[s->ref_idx];
					const tab_cell_t **row_theirs = tab_theirs->array[oppo->ref_idx];
					for (int j = 0; j < n_cols; j++) {
						const tab_col_desc_t *pcol = &final_cols[j];
						const tab_cell_t *tmp_l = pcol->ref_idx_ours >= 0 ? row_ours[pcol->ref_idx_ours] : tab_cell_new("", 0);
						const tab_cell_t *tmp_r = pcol->ref_idx_theirs >= 0 ? row_theirs[pcol->ref_idx_theirs] : tab_cell_new("", 0);
						if (cell_cmp(tmp_l, tmp_r))
						{
							conf_flag = 1;
							break;
						}
					}
				}
				if (conf_flag)
				{
					s->res = conflict_priority[s->mt][oppo->mt][0];
				}
				else
				{
					s->res = TAB_RES_TAKE_THIS;
					oppo->res = TAB_RES_TAKE_OPPO;
				}
			}
		}
		for (int j = 0; j < ts->n_right; j++) {
			struct tab_action *s = &ts->action_right[j];
			debug_report_action(s, i, "RIGHT");
			if (s->res != TAB_RES_UNDICIDED)
				continue;
			if (!s->conflict_with) {
				s->res = TAB_RES_TAKE_THIS;
			}
			else {
				struct tab_action *oppo = s->conflict_with;
				int conf_flag = 1;
				if (s->mt == TAB_MOD_INSERT && oppo->mt == TAB_MOD_INSERT)
				{
					conf_flag = 0;
					const tab_cell_t **row_ours = tab_ours->array[oppo->ref_idx];
					const tab_cell_t **row_theirs = tab_theirs->array[s->ref_idx];
					for (int j = 0; j < n_cols; j++) {
						const tab_col_desc_t *pcol = &final_cols[j];
						const tab_cell_t *tmp_l = pcol->ref_idx_ours >= 0 ? row_ours[pcol->ref_idx_ours] : tab_cell_new("", 0);
						const tab_cell_t *tmp_r = pcol->ref_idx_theirs >= 0 ? row_theirs[pcol->ref_idx_theirs] : tab_cell_new("", 0);
						if (cell_cmp(tmp_l, tmp_r))
						{
							conf_flag = 1;
							break;
						}
					}
				}
				if (conf_flag)
				{
					s->res = conflict_priority[oppo->mt][s->mt][1];
				}
				else
				{
					s->res = TAB_RES_TAKE_THIS;
					oppo->res = TAB_RES_TAKE_OPPO;
				}
			}
		}
	}

	for (int i = 0; i < n; i++) {
		struct tab_action_chunk *ts = &tsc[i];
		for (int j = 0; j < ts->n_left; j++) {
			struct tab_action *s = &ts->action_left[j];
			if (s->res == TAB_RES_CONFLICT)
				return 1;
		}
		for (int j = 0; j < ts->n_right; j++) {
			struct tab_action *s = &ts->action_right[j];
			if (s->res == TAB_RES_CONFLICT)
				return 1;
		}
	}
	return 0;
}

static int merge_actions(tab_merge_context_t *mcontext
	, const tab_desc_t *tab_parent, const tab_desc_t *tab_ours, const tab_desc_t *tab_theirs
	, struct tab_action_chunk tac[], int n)
{
	const primary_key_t **pkp_parent = tab_get_primary_key_array(mcontext->pkm_parent);
	const primary_key_t **pkp_ours = tab_get_primary_key_array(mcontext->pkm_ours);
	const primary_key_t **pkp_theirs = tab_get_primary_key_array(mcontext->pkm_theirs);
	int nparent = tab_parent->row;
	int nours = tab_ours->row;
	int ntheirs = tab_theirs->row;

	tab_row_desc_t *cursor = NULL;
	const int init_size = sizeof(tab_row_desc_t) * (nours + ntheirs);
	mcontext->final_rows = cursor = (tab_row_desc_t *)tab_alloc(init_size);// output;

	for (int i = 0; i < n; i++) {
		struct tab_action_chunk *ts_cur = &tac[i];
		if (ts_cur->tp.stable) {
			for (int j = 0; j <= ts_cur->tp.o.to - ts_cur->tp.o.from; j++) {
				cursor->pk_name = pkp_parent[ts_cur->tp.o.from + j]->pkdata;
				cursor->ref_idx_parent = ts_cur->tp.o.from + j;

				cursor->ref_name_ours = pkp_ours[ts_cur->tp.a.from + j]->pkdata;
				cursor->ref_idx_ours = ts_cur->tp.a.from + j;
				cursor->ref_type_ours = TAB_MOD_NONE;

				cursor->ref_name_theirs = pkp_theirs[ts_cur->tp.b.from + j]->pkdata;
				cursor->ref_idx_theirs = ts_cur->tp.b.from + j;
				cursor->ref_type_theirs = TAB_MOD_NONE;

				cursor++;
			}
		}
		else {
			sub_merge_action(&cursor, ts_cur->action_left, ts_cur->n_left,
				pkp_theirs, ntheirs, OFFSETOF(tab_col_desc_t, ref_name_ours), OFFSETOF(tab_col_desc_t, ref_idx_ours), OFFSETOF(tab_col_desc_t, ref_type_ours),
				OFFSETOF(tab_col_desc_t, ref_name_theirs), OFFSETOF(tab_col_desc_t, ref_idx_theirs), OFFSETOF(tab_col_desc_t, ref_type_theirs),
				"ours");
			sub_merge_action(&cursor, ts_cur->action_right, ts_cur->n_right,
				pkp_ours, nours, OFFSETOF(tab_col_desc_t, ref_name_theirs), OFFSETOF(tab_col_desc_t, ref_idx_theirs), OFFSETOF(tab_col_desc_t, ref_type_theirs),
				OFFSETOF(tab_col_desc_t, ref_name_ours), OFFSETOF(tab_col_desc_t, ref_idx_ours), OFFSETOF(tab_col_desc_t, ref_type_ours),
				"theirs");
		}
	}

	mcontext->nrows = (int)(cursor - mcontext->final_rows);
	return mcontext->nrows;
}

void display_cell_conflicts(const tab_cell_t *pkrow, const tab_cell_t *cell_parent, const tab_cell_t *cell_ours, const tab_cell_t *cell_theirs, int, int);
void display_schema_conflicts(const tab_desc_t *tab_parent, const tab_desc_t *tab_ours, const tab_desc_t *tab_theirs, struct tab_schema_chunk schema_hunks[], int nchunk);
int display_action_conflicts(const tab_desc_t *tab_parent, const tab_desc_t *tab_ours, const tab_desc_t *tab_theirs, struct tab_action_chunk action_hunks[], int nchunk);


static tab_desc_t *build_xcross_ref_result(tab_merge_context_t *mcontext
	, const tab_desc_t *tab_parent
	, const tab_desc_t *tab_ours
	, const tab_desc_t *tab_theirs)
{
	int ncols = mcontext->ncols;
	int nrows = mcontext->nrows;
	tab_col_desc_t *final_cols = mcontext->final_cols;
	tab_row_desc_t *final_rows = mcontext->final_rows;

	const primary_key_map_t *pkm_parent = mcontext->pkm_parent;
	const primary_key_map_t *pkm_ours = mcontext->pkm_ours;
	const primary_key_map_t *pkm_theirs = mcontext->pkm_theirs;

	tab_desc_t *ret = (tab_desc_t *)tab_alloc(sizeof(tab_desc_t));
	ret->hdr_cell = (tab_cell_t **)tab_alloc(sizeof(tab_cell_t *) * ncols);
	ret->col = ncols;
	ret->row = nrows;
	ret->npk = tab_ours->npk;
	memcpy(ret->pk_idxes, tab_ours->pk_idxes, sizeof(ret->pk_idxes));

	mcontext->ncell_conflict = 0;

	for (int i = 0; i < ncols; i++) {
		ret->hdr_cell[i] =  tab_cell_ref(final_cols[i].col_name);
	}
	ret->array = (tab_cell_t ***)tab_alloc(sizeof(tab_cell_t **) * nrows);
	for (int i = 0; i < nrows; i++) {
		const tab_row_desc_t *prow = &final_rows[i];
		ret->array[i] = (tab_cell_t **)tab_alloc(sizeof(tab_cell_t *) * ncols);
		const tab_cell_t **row_ours, **row_theirs, **row_parent;
		if (i < 2) {
			row_ours = tab_ours->array[i];
			row_theirs = tab_theirs->array[i];
			row_parent = tab_parent->array[i];
		} else {
			row_ours = find_row_by_pk(pkm_ours, prow->pk_name);
			row_theirs = find_row_by_pk(pkm_theirs, prow->pk_name);
			row_parent = find_row_by_pk(pkm_parent, prow->pk_name);
		}

		for (int j = 0; j < ncols; j++) {
			const tab_col_desc_t *pcol = &final_cols[j];
			tab_cell_t *cell_ours = NULL, *cell_theirs = NULL, *cell_parent = NULL;

			//insert by theirs
			if (NULL != row_ours) {
				if (pcol->ref_idx_ours >= 0) {
					cell_ours = row_ours[pcol->ref_idx_ours];
				}
				else {
					cell_ours = NULL;
				}
			}
			if (NULL != row_theirs) {
				if (pcol->ref_idx_theirs >= 0) {
					cell_theirs = row_theirs[pcol->ref_idx_theirs];
				}
				else {
					cell_theirs = NULL;
				}
			}
			if (cell_ours && !cell_theirs){
				ret->array[i][j] = tab_cell_ref(cell_ours);
			}
			else if (cell_theirs && !cell_ours) {
				ret->array[i][j] = tab_cell_ref(cell_theirs);
			}
			else
			{
				cell_ours = cell_ours ? cell_ours : &null_cell;
				cell_theirs = cell_theirs ? cell_theirs : &null_cell;
				if (0 == cell_cmp(cell_ours, cell_theirs)) {
					ret->array[i][j] = tab_cell_ref(cell_theirs);
				}
				else {
					assert(pcol->ref_idx_parent >= 0);
					cell_parent = row_parent[pcol->ref_idx_parent];
					//cell only modified by us;
					if (0 != cell_cmp(cell_parent, cell_ours) && 0 == cell_cmp(cell_parent, cell_theirs)){
						ret->array[i][j] = tab_cell_ref(cell_ours);
					}
					//cell only modified by them;
					else if (0 == cell_cmp(cell_parent, cell_ours) && 0 != cell_cmp(cell_parent, cell_theirs)){
						ret->array[i][j] = tab_cell_ref(cell_theirs);
					}
					//both modified
					else {
						ret->array[i][j] = tab_cell_new_conflict(cell_ours, cell_theirs);
						mcontext->ncell_conflict++;
					}
				}
			}
		}
	}
	return ret;
}

/*intermediate result = result that 3 orignal tables are shifted to as though they are of the same column layouts*/
static 
tab_desc_t *build_intermediate_result(const tab_desc_t *tab_old, int as,
	int ncols, const tab_col_desc_t final_cols[])
{
	int nrows = tab_old->row;
	tab_desc_t *ret = (tab_desc_t *)tab_alloc(sizeof(tab_desc_t));
	ret->hdr_cell = (tab_cell_t **)tab_alloc(sizeof(tab_cell_t *) * ncols);
	ret->col = ncols;
	ret->npk = tab_old->npk;
	memcpy(ret->pk_idxes, tab_old->pk_idxes, sizeof(ret->pk_idxes));

	for (int i = 0; i < ncols; i++) {
		ret->hdr_cell[i] = tab_cell_ref(final_cols[i].col_name);
	}

	ret->array = (tab_cell_t ***)tab_alloc(sizeof(tab_cell_t **) * tab_old->row);
	for (int i = 0; i < nrows; i++) {
		ret->array[i] = (tab_cell_t **)tab_alloc(sizeof(tab_cell_t *) * ncols);
		tab_cell_t **row_new = ret->array[i];
		const tab_cell_t **row_old = tab_old->array[i];

		for (int j = 0; j < ncols; j++) {
			const tab_col_desc_t *pcol = &final_cols[j];
			int idx = -1; 
			switch (as) {
			case TAB_INPUT_PARENT:
				idx = pcol->ref_idx_parent;
				break;
			case TAB_INPUT_OURS:
				idx = pcol->ref_idx_ours;
				break;
			case TAB_INPUT_THEIRS:
				idx = pcol->ref_idx_theirs;
				break;
			}
			row_new[j] = idx >= 0 ? tab_cell_ref(row_old[idx]) : tab_cell_ref(&null_cell);
		}
	}
	ret->row = nrows;
	return ret;
}

/*
	this routine is to apply row change conflict taking place in B.tab to A.tab.
	for instance
		P.tab		[ID* as primary key]
		Ours.tab		+ row	[ID*	= 9002,		name = pxf_god,		age=28]
		Theirs.tab		+ row	[ID*	= 9002,		name = june,		age=25]

	we make change as if
		Ours.tab		+ row	[ID*	= 9002,		name = pxf_god,		age=28]
		Ours.tab		+ row	[ID*	= 9002,		name = june,		age=25]
		above	rows are adjacent so that convenient to see difference between them.
		Theirs.tab		do nothing
*/
static void shift_identical_insertion(const tab_merge_context_t *mcontext, tab_desc_t *tab_dest, tab_desc_t *tab_src, int nchunk) {
	int	n_same_insertion = 0;
	int new_row = 0;
	int n_capacity = 0;
	tab_cell_t **row_erase[0x1000];
	for (int i = 0; i < nchunk; i++)
	{
		struct tab_action_chunk *phunk = &mcontext->action_hunks[i];
		if (!phunk->tp.stable) {
			for (int j = 0; j < phunk->n_left; j++) {
				struct tab_action *cur = &phunk->action_left[j];
				if (cur->res == TAB_RES_CONFLICT) {
					struct tab_action *conf = cur->conflict_with;
					if (cur->mt == TAB_MOD_INSERT && conf->mt == TAB_MOD_INSERT) {
						row_erase[n_same_insertion] = tab_src->array[conf->ref_idx];
						tab_src->array[conf->ref_idx] = NULL;
						++n_same_insertion;
					}
				}
			}
		}
	}

	//shrink
	tab_cell_t ***array_src = tab_src->array;
	for (int i = 0, j = 0
		; j < tab_src->row
		; i++, j++)
	{
		while (j < tab_src->row && array_src[j] == NULL) j++;
		if (j >= tab_src->row) break;
		array_src[i] = array_src[j];
	}

	tab_cell_t ***new_array_dest = (tab_cell_t ***)tab_alloc(sizeof(void **) * tab_dest->row + n_same_insertion);
	int	k = 0;
	for (int i = 0; i < tab_dest->row; i++)
	{
		new_array_dest[k++] = tab_dest->array[i];
		for (int j = 0; j < n_same_insertion; j++)
		{
			if (1) {
				new_array_dest[k++] = row_erase[j];
				row_erase[j] = NULL;
			}
			//row_erase[j]
		}
	}
	tab_free(tab_dest->array);
	tab_dest->array = new_array_dest;
}

tab_desc_t *tab_merge_whole(const tab_desc_t *tab_parent, const tab_desc_t *tab_ours, const tab_desc_t *tab_theirs, int mode, int *ncell_conflict, tab_desc_t *tab_semi[3]) {
	int nchunk;
	int unsolved;
	int n_cols, n_rows;

	//phrase 0, preparation: build context;
	tab_merge_context_t *mcontext = _make_merge_context(MAX_ACTION_CHUNK);

	//phrase 1 BEGIN, to decide columns exist in final. And where are those columns from respectively.
	///////////////////////////////////////////////////////////
	nchunk = generate_schema_chunks(mcontext, tab_parent, tab_ours, tab_theirs);
	struct tab_schema_chunk *schema_hunks = mcontext->schema_hunks;

//remerge_schemas:
	unsolved = solve_schemas_conflict(schema_hunks, nchunk);
	if (unsolved) {
		//resolve schemas_dialog
		if (mode == MERGE_MODE_PROB)
		{
			_delete_merge_context(mcontext);
			return NULL;
		}
		display_schema_conflicts(tab_parent, tab_ours, tab_theirs, schema_hunks, nchunk);
		_delete_merge_context(mcontext);
		return NULL;
		//interactively solve conflicts...
		//goto remerge_schemas;
	}
	n_cols = merge_schemas(mcontext, tab_parent, tab_ours, tab_theirs, schema_hunks, nchunk);
	//phrase 1 END

	//phrase 2 BEGIN, to decide rows exist in final. And where are those rows from respectively.
	/////////////////////////////////////////////////////////////
	nchunk = generate_action_chunks(mcontext, tab_parent, tab_ours, tab_theirs);
	struct tab_action_chunk *action_hunks = mcontext->action_hunks;
	primary_key_t **pkp_parent = tab_get_primary_key_array(mcontext->pkm_parent);
	primary_key_t **pkp_ours = tab_get_primary_key_array(mcontext->pkm_ours);
	primary_key_t **pkp_theirs = tab_get_primary_key_array(mcontext->pkm_theirs);

//remerge_actions:
	unsolved = solve_actions_conflict(action_hunks, nchunk, tab_ours, tab_theirs, n_cols, mcontext->final_cols);
	if (unsolved) {
		//resolve
		if (mode == MERGE_MODE_PROB)
		{
			_delete_merge_context(mcontext);
			return NULL;
		}
		int choice = display_action_conflicts(tab_parent, tab_ours, tab_theirs, action_hunks, nchunk);

		if (NULL != tab_semi) {
			tab_semi[0] = tab_semi[1] = tab_semi[2] = NULL;
			
			switch (choice) {
			case 0:
				break;
			case 1:
			case 2:
			case 3:
			{
				tab_desc_t *parent_new = build_intermediate_result(tab_parent, TAB_INPUT_PARENT, n_cols, mcontext->final_cols);
				tab_desc_t *ours_new = build_intermediate_result(tab_ours, TAB_INPUT_OURS, n_cols, mcontext->final_cols);
				tab_desc_t *theirs_new = build_intermediate_result(tab_theirs, TAB_INPUT_THEIRS, n_cols, mcontext->final_cols);

				if (choice == 2) {
					shift_identical_insertion(mcontext, ours_new, theirs_new, nchunk);
				}
				else if (choice == 3) {
					shift_identical_insertion(mcontext, theirs_new, ours_new, nchunk);
				}

				tab_semi[0] = parent_new;
				tab_semi[1] = ours_new;
				tab_semi[2] = theirs_new;
			}
			default:
				break;
			}
		}
		return NULL;
		//goto remerge_actions;
	}
	n_rows = merge_actions(mcontext, tab_parent, tab_ours, tab_theirs, action_hunks, nchunk);
	//phrase 2 END

	//phrase 3 according to the sources of columns and rows. for each cell, build a new table desc.
	tab_desc_t *tab_merged = build_xcross_ref_result(mcontext, tab_parent, tab_ours, tab_theirs);
	_delete_merge_context(mcontext);
	return tab_merged;
}
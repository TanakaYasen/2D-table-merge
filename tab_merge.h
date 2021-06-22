#ifndef _TAB_MERGE_H
#define _TAB_MERGE_H

#include "core_algorithm.h"
#include "tab_data.h"

#define MERGE_MODE_EXEC 0
#define MERGE_MODE_PROB 1

typedef enum tab_modify_result {
	TAB_RES_UNDICIDED,		//未决
	TAB_RES_CONFLICT,		//无冲突
	TAB_RES_TAKE_THIS,		//接受这个
	TAB_RES_TAKE_OPPO,		//接受对面
	TAB_RES_TAKE_MASTER,	//两个接受
	TAB_RES_TAKE_SLAVE,		//两个接受
	TAB_RES_TAKE_NEITHER,	//两个都否决
	TAB_RES_IMPOSSIBLE,		//这个要abort()
}tab_modify_result_t;

struct tab_schema_chunk;
struct tab_action_chunk;

struct tab_schema {
	tab_modify_type_t			mt;
	tab_modify_result_t			res;
	int							ref_idx;
	struct tab_schema_chunk		*phunk;
	struct tab_schema			*conflict_with;
	union {
		tab_cell_t *pcell;
		struct {
			tab_cell_t *cell_insert;
			int chunk_insert;
		};
		struct {
			tab_cell_t *cell_remove;
			int chunk_remove;
		};
		struct {
			tab_cell_t *cell_old, *cell_new;
			int chunk_rename;
		};
		struct {
			tab_cell_t *cell_swap;
			int from_chk, to_chk;
		};
	};
};

struct tab_schema_chunk {
	struct tuple			tp;
	union {
		struct {
			int						n_left;
			int						n_right;
			int						conflict;
			int						change_left;
			int						change_right;
			struct tab_schema		*schema_left;//NULL == no changes
			struct tab_schema		*schema_right;//NULL == no changes
		};
		struct {
			int						nstable;
			tab_cell_t				**pcells;
		};
	};
};

struct tab_action {
	tab_modify_type_t		mt;
	tab_modify_result_t		res;
	int						ref_idx;
	struct tab_action_chunk *phunk;
	struct tab_action		*conflict_with;

	union {
		const tab_cell_t *pcell;
		struct {
			const tab_cell_t *cell_insert;
			int chunk_insert;
		};
		struct {
			const tab_cell_t *cell_remove;
			int chunk_remove;
		};
		struct {
			const tab_cell_t *cell_swap;
		};
	};
};

struct tab_action_chunk {
	struct tuple			tp;
	int						n_left;
	int						n_right;
	struct tab_action		*action_left;//NULL == no changes
	struct tab_action		*action_right;//NULL == no changes
};

tab_desc_t *tab_merge_whole(const tab_desc_t *tab_parent, const tab_desc_t *tab_ours, const tab_desc_t *tab_theirs, int mode, int *ncell_conflict, tab_desc_t *tab_semi[3]);

#endif

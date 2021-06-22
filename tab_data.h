#ifndef _TAB_DATA_H
#define _TAB_DATA_H

#include <stdlib.h>
#include <stdint.h>

#define TAB_DATA_ROW		2
#define PKMAX				17 //–“‘À ˝◊÷
#define MAX_LEN				40000
#define MAX_ROW				40000
#define MAX_COL				512

typedef enum tab_modify_type {
	TAB_MOD_NONE,	//no change
	TAB_MOD_INSERT,	//≤Â»Î
	TAB_MOD_RENAME,	//÷ÿ√¸√˚
	TAB_MOD_SWAP_IN,	//ªª–Ú
	TAB_MOD_SWAP_OUT,	//ªª–Ú
	TAB_MOD_REMOVE,	//“∆≥˝
}tab_modify_type_t;

typedef struct tab_cell {
	unsigned	refcount;
	unsigned	hash;
	size_t		len;
	uint8_t		data[0];
}tab_cell_t;


//table descriptor
typedef struct tab_desc {
	int			col, row;
	tab_cell_t	**hdr_cell;
	tab_cell_t	***array;

	int npk;
	int pk_idxes[PKMAX];
}tab_desc_t;


struct tab_dup {
	tab_cell_t *cell;
	int dup;
	int def;
};

typedef struct tab_bug { 
	int				ncell_conf;
	int				ncol, nrow;
	struct tab_dup *col_bug;
	struct tab_dup *row_bug;
}tab_bug_t;

typedef struct primary_key {
	unsigned int		hash;
	int					nkeys;
	tab_cell_t	*pkdata;
	tab_cell_t	**prow;
}primary_key_t;

typedef struct primary_key_map primary_key_map_t;
primary_key_t	**tab_get_primary_key_array(primary_key_map_t *dict);

//returns 0 if (a == b)
extern tab_cell_t null_cell;

tab_cell_t *tab_cell_new(const char *data, size_t len);
tab_cell_t *tab_cell_new_conflict(const tab_cell_t *a, const tab_cell_t *b);
tab_cell_t *tab_cell_ref(tab_cell_t *cell);
tab_cell_t *tab_cell_unref(tab_cell_t *cell);



tab_desc_t *tab_desc_read_from_stream(const char *content, size_t fs);
tab_desc_t *tab_desc_read_from_file(const char *tab_file);

int					tab_desc_guess_pk(tab_desc_t *tab_org, int assigned_num, int *assigned_arr);
const tab_bug_t		*tab_desc_verify(const tab_desc_t *tab_org);
void				*tab_bug_del(tab_bug_t *tb);


//order:must "asc" / "dsc", otherwise, return NULL;
const tab_desc_t	*tab_desc_sort(const tab_desc_t *tab_old, const char *order);
void				tab_desc_del(tab_desc_t *tab);
int					tab_desc_write_invalid(const char *fname);
void				tab_desc_write_to_file(const tab_desc_t *tab, const char *tab_file);
//delete
void				*tab_desc_write_to_stream(const tab_desc_t *tab, size_t *sz);
void				tab_free_stream(void *stream);

int					cell_cmp(const tab_cell_t *a, const tab_cell_t *b);
int					pk_cmp(const primary_key_t *pka, const primary_key_t *pkb);


tab_cell_t			*gen_primary_cell(const tab_desc_t *tab_org, const tab_cell_t **row);
primary_key_map_t	*tab_build_primary_key_map(const tab_desc_t *tab_source);
void				tab_kill_primary_key_map(primary_key_map_t *map);

const tab_cell_t	**find_row_by_pk(const primary_key_map_t *pkm, const tab_cell_t *pk);

int					tab_desc_check_same_pk2(const tab_desc_t *a, const tab_desc_t *b);
int					tab_desc_check_same_pk3(const tab_desc_t *a, const tab_desc_t *b, const tab_desc_t *c);

const char			*sharp_split_whole_pk(tab_cell_t *pc); 

#endif


#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <memory.h>

#include "tab_data.h"

#include "ks/3rd/uthash.h"

#define CELL_CONFLICT_MARK	"<<<ours===theirs>>>"

struct pk_hash {
	UT_hash_handle hh_;
	const tab_cell_t **trow;
	int	 def_idx;
	char pk[0];
};

struct colname_hash {
	UT_hash_handle hh_;
	int	 def_idx;
	char name[0];
};

struct primary_key_map {
	const tab_desc_t		*source;
	struct pk_hash			*pkhh;

	int						nkeys;
	primary_key_t			**pkeyarr;
	primary_key_t			*keyarr;
};

struct tab_sort_row {
	int npk;
	struct {
		char				*data;
		unsigned long long	num;
	}pkcols[PKMAX];
	const tab_cell_t **prow;
};

#define FLAG_NEWCELL 0
#define FLAG_NEWLINE 1
#define FLAG_EOF 2
#define FLAG_UNEXPECT 3

#define tab_alloc		malloc
#define tab_free		free
#define tab_realloc		realloc

tab_cell_t null_cell = {
	1, 42, 0, ""
};

static unsigned int BKDRHash(char *str, size_t len)
{
	unsigned int seed = 131; // 31 131 1313 13131 131313 etc..
	unsigned int hash = 0;
	const char *se = str + len;
	while (str < se)
		hash = hash * seed + (*str++);
	return (hash & 0x7FFFFFFF);
}

tab_cell_t *tab_cell_new(const char *data, size_t len) {
	tab_cell_t *ret = tab_alloc(sizeof(tab_cell_t) + len + 1);
	if (NULL == ret) return NULL;
	memcpy(ret->data, data, len);
	ret->refcount = 1;
	ret->len = len;
	ret->data[len] = '\0';
	ret->hash = BKDRHash(ret->data, len);
	return ret;
}

tab_cell_t *tab_cell_new_conflict(const tab_cell_t *a, const tab_cell_t *b) {
	tab_cell_t *ret = tab_alloc(sizeof(tab_cell_t) + a->len + sizeof(CELL_CONFLICT_MARK) + b->len + 1);
	if (NULL == ret) return NULL;
	memset(ret->data, 0, a->len + sizeof(CELL_CONFLICT_MARK) + b->len + 1);
	strcat(ret->data, a->data);
	strcat(ret->data, CELL_CONFLICT_MARK);
	strcat(ret->data, b->data);
	ret->refcount = 1;
	ret->len = strlen(ret->data);
	ret->hash = BKDRHash(ret->data, ret->len);
	return ret;
}

tab_cell_t *tab_cell_ref(tab_cell_t *tcell) {
	tcell->refcount++;
	return tcell;
}

tab_cell_t *tab_cell_unref(tab_cell_t *tcell) {
	tcell->refcount--;
	if (0U == tcell->refcount) {
		tab_free(tcell);
		return NULL;
	}
	return tcell;
}

static tab_cell_t *read_a_cell(const char **stream, const char * const pe, int *flag) {
	char __cell_cache__[4 * 1024];
	char *pcache = __cell_cache__;
	char *pc = NULL;

	const char *cs = *stream;//cell start
	const char *ce = NULL;//cell end
	const char *ps = cs;
	tab_cell_t *ret_val = NULL;

	for (; ps < pe;) {
		if (*ps == '\r') {
			ce = ps;
			ps++;
			//while (*ps == '\r') ps++;
			if (ps < pe && *ps == '\n') ps++;
			*flag = FLAG_NEWLINE;
			break;
		}
		else if (*ps == '\n') {
			ce = ps;
			ps++;
			*flag = FLAG_NEWLINE;
			break;
		}
		else if (*ps == '\t') {
			ce = ps;
			ps++;
			*flag = FLAG_NEWCELL;
			break;
		}
		ps++;
	}
	if (ps >= pe) {
		*flag = FLAG_EOF;
		ce = ce ? ce:pe;
	}
	*stream = ps;

	if (ce - cs > sizeof(__cell_cache__))
		pcache = (char *)malloc(ce - cs);

//rescan
	ps = cs;
	pc = pcache;
	for (; ps < ce;) 
		*pc++ = *ps++;

	ret_val = tab_cell_new(pcache, pc - pcache);
	if (pcache != __cell_cache__)
		free(pcache);

	return ret_val;
}


tab_desc_t *tab_desc_read_from_stream(const char *content, size_t fs) {
	const char *cursor = content;
	if (fs <= 3) return NULL;
	const char *pe = content + fs;
	char BOM[] = { 0xef, 0xbb, 0xbf };
	if (0 == memcmp(BOM, cursor, 3))
		cursor += 3;

	//read header
	int ncol = 0;
	int nrow = 0;
	int flag = FLAG_NEWCELL;

	tab_desc_t *ret = (tab_desc_t *)tab_alloc(sizeof(tab_desc_t));
	ret->npk = 0;

	tab_cell_t **hdr = NULL;
	tab_cell_t ***arr = NULL;
	while (flag != FLAG_NEWLINE) {
		hdr = tab_realloc(hdr, (ncol + 1)*sizeof(tab_cell_t *));
		hdr[ncol] = read_a_cell(&cursor, pe, &flag);
		if (flag >= FLAG_EOF) {
			return NULL;
		}
		ncol++;
	}

	while (flag != FLAG_EOF) {
		arr = tab_realloc(arr, (nrow + 1) * sizeof(tab_cell_t **));
		arr[nrow] = (tab_cell_t **)tab_alloc(sizeof(tab_cell_t *) * ncol);
		int j = 0;
		for (j = 0; j < ncol; j++) {
			tab_cell_t *cell = read_a_cell(&cursor, pe, &flag);
			if (nrow == 0) {//line 2, namely the comment line, figure out PrimaryKeys.
				if (cell->len > 0 && cell->data[0] == '*') {
					for (int k = 1; k < cell->len; k++)
						cell->data[k - 1] = cell->data[k]; //remove ahead '*'
					cell->data[cell->len - 1] = '\0';
					cell->len--;
					cell->hash = BKDRHash(cell->data, cell->len);

					ret->npk++;
					if (ret->npk > PKMAX)
						return NULL;
					ret->pk_idxes[ret->npk - 1] = j;
				}
			}
			if (flag == FLAG_NEWLINE) {//not a complete line.
				arr[nrow][j++] = cell;
				for (; j < ncol; j++) arr[nrow][j] = tab_cell_new("", 0);
				break;
			} else if (flag == FLAG_EOF) {
				arr[nrow][j++] = cell;
				for (; j < ncol; j++) arr[nrow][j] = tab_cell_new("", 0);
				break;
			} else
				arr[nrow][j] = cell;
		}
		while (flag != FLAG_NEWLINE && flag != FLAG_EOF) {
			tab_cell_unref(read_a_cell(&cursor, pe, &flag));
		}
		if (j < ncol - 1)			//not a complete line.
			if (nrow < 2) {
				for (; j < ncol; j++) arr[nrow][j] = tab_cell_new("", 0);
				nrow++;
				break;
			}
			else
				tab_free(arr[nrow]);	//discard
		else
			nrow++;					//accept
	}

	while (nrow < 2) {
		arr = tab_realloc(arr, (nrow + 1) * sizeof(tab_cell_t **));
		arr[nrow] = (tab_cell_t **)tab_alloc(sizeof(tab_cell_t *) * ncol);
		for (int j = 0; j < ncol;j++) arr[nrow][j] = tab_cell_new("", 0);
		nrow++;
	}

	ret->col = ncol;
	ret->row = nrow;
	ret->array = arr;
	ret->hdr_cell = hdr;

	return ret;
}

tab_desc_t *tab_desc_read_from_file(const char *tab_file) {
	FILE *fp = fopen(tab_file, "rb");
	if (NULL == fp) return NULL;
	fseek(fp, 0, SEEK_END);
	long fs = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	char *content = malloc(fs);
	fread(content, fs, 1, fp);
	fclose(fp);
	tab_desc_t *retval = tab_desc_read_from_stream(content, (size_t)fs);
	free(content);

	return retval;
}

void tab_desc_write_to_file(const tab_desc_t *t, const char *tab_file) {
	FILE *fp = fopen(tab_file, "wb");
	if (NULL == fp) return;
	char BOM[] = { 0xef, 0xbb, 0xbf };
	fwrite(BOM, 3, 1, fp);
	for (int i = 0; i < t->col; i++) {
		fwrite(t->hdr_cell[i]->data, t->hdr_cell[i]->len, 1, fp);
		if (i != t->col-1)
			fwrite("\t", 1, 1, fp);
	}
	fwrite("\r\n", 2, 1, fp);
	for (int i = 0; i < t->row; i++) {
		for (int j = 0; j < t->col; j++) {
			for (int k = 0; k < t->npk; k++) {
				if (i == 0 && j == t->pk_idxes[k]) {
					fwrite("*", 1, 1, fp);
					break;
				}
			}
			fwrite(t->array[i][j]->data, t->array[i][j]->len, 1, fp);
			if (j != t->col-1)
				fwrite("\t", 1, 1, fp);
		}
		fwrite("\r\n", 2, 1, fp);
	}
	fclose(fp);
}


// always assume realloc OK
#define _ss_write(m, s) {size_t off = pos-buf; \
	if (off + s > sz) \
		{sz = sz*2 > off+s ? sz*2 : off+s; buf = (uint8_t *)tab_realloc(buf, sz); pos = buf + off;}\
	memcpy(pos, m, s); pos += s;}

void *tab_desc_write_to_stream(const tab_desc_t *t, size_t *ssz) {
	size_t sz = 1024 * 1024;
	uint8_t *buf = tab_alloc(1024 * 1024);
	uint8_t *pos = buf;

	char BOM[] = { 0xef, 0xbb, 0xbf };
	_ss_write(BOM, 3);

	for (int i = 0; i < t->col; i++) {
		_ss_write(t->hdr_cell[i]->data, t->hdr_cell[i]->len);
		if (i != t->col - 1)
			_ss_write("\t", 1);
	}
	_ss_write("\r\n", 2);
	for (int i = 0; i < t->row; i++) {
		for (int j = 0; j < t->col; j++) {
			for (int k = 0; k < t->npk; k++) {
				if (i == 0 && j == t->pk_idxes[k]) {
					_ss_write("*", 1);
					break;
				}
			}
			_ss_write(t->array[i][j]->data, t->array[i][j]->len);
			if (j != t->col - 1)
				_ss_write("\t", 1);
		}
		_ss_write("\r\n", 2);
	}
	*ssz = pos - buf;
	return buf;
}

void tab_free_stream(void *stream) {
	tab_free(stream);
}

void tab_desc_del(tab_desc_t *tab)
{
	for (int j = 0; j < tab->col; j++)
	{
		tab_cell_unref(tab->hdr_cell[j]);
	}
	tab_free(tab->hdr_cell);
	for (int i = 0; i < tab->row; i++)
	{
		for (int j = 0; j < tab->col; j++)
		{
			tab_cell_unref(tab->array[i][j]);
		}
		tab_free(tab->array[i]);
	}
	tab_free(tab->array);
}

int cell_cmp(const tab_cell_t *a, const tab_cell_t *b) {
	return 0 == (a->hash == b->hash && a->len == b->len && 0 == memcmp(a->data, b->data, a->len));
}

int pk_cmp(const primary_key_t *pka, const primary_key_t *pkb) {
	assert(pka->nkeys == pkb->nkeys);
	if (pka->hash != pkb->hash)
		return 1;
	if (0 != cell_cmp(pka->pkdata, pkb->pkdata))
		return 1;
	return 0;
}

static int pk_cmp_asc(const struct tab_sort_row *pka, const struct tab_sort_row *pkb) {
	int npk = pka->npk;
	for (int i = 0; i < npk; i++)
	{
		if (pka->pkcols[i].data && pkb->pkcols[i].data) {
			int r = strcmp(pka->pkcols[i].data, pkb->pkcols[i].data);
			if (0 == r)
				continue;
			return r;
		}
		else {
			long long r = (long long)pka->pkcols[i].num - (long long)pkb->pkcols[i].num;
			if (0 == r)
				continue;
			return (int)r;
		}
	}
	return 0;
}

static int pk_cmp_dsc(const struct tab_sort_row *pka, const struct tab_sort_row *pkb) {
	int npk = pkb->npk;
	for (int i = 0; i < npk; i++)
	{
		if (pka->pkcols[i].data && pkb->pkcols[i].data) {
			int r = strcmp(pka->pkcols[i].data, pkb->pkcols[i].data);
			if (0 == r)
				continue;
			return -r;
		}
		else {
			long long r = (long long)pka->pkcols[i].num - (long long)pkb->pkcols[i].num;
			if (0 == r)
				continue;
			return (int)-r;
		}
	}
	return 0;
}

static size_t fill_pk(const tab_desc_t *tab_org, const tab_cell_t **line, char *buf) {
	char *p = buf;
	for (int j = 0; j < tab_org->npk; j++) {
		int col_idx = tab_org->pk_idxes[j];
		assert(col_idx < tab_org->col);
		memcpy(p, line[col_idx]->data, line[col_idx]->len);
		p += line[col_idx]->len;
		*p++ = '\0';
	}
	return p - buf;
}

tab_cell_t *gen_primary_cell(const tab_desc_t *tab_org, const tab_cell_t **row) {
	char data_concat[4096];
	size_t pklen = fill_pk(tab_org, row, data_concat);
	return tab_cell_new(data_concat, pklen);
}

primary_key_map_t * tab_build_primary_key_map(const tab_desc_t *tab_source) {
	int pk = tab_source->npk;
	int count = tab_source->row;
	assert(pk > 0);

	primary_key_map_t *ret = (primary_key_map_t *)tab_alloc(sizeof(primary_key_map_t));
	ret->source = tab_source;
	ret->pkhh = NULL;
	primary_key_t *keyarr = (primary_key_t *)tab_alloc(sizeof(primary_key_t) * count);
	primary_key_t **pkeyarr = (primary_key_t **)tab_alloc(sizeof(primary_key_t*) * count);
	ret->nkeys = count;
	ret->keyarr = keyarr;
	ret->pkeyarr = pkeyarr;

	//bug > 4096?
	char data_concat[4096];
	for (int i = 0; i < count; i++) {
		const tab_cell_t **row = tab_source->array[i];
		primary_key_t *cur = &keyarr[i] ;
		pkeyarr[i] = cur;
		size_t pklen = fill_pk(tab_source, tab_source->array[i], data_concat);
		cur->nkeys = pk;
		cur->hash = BKDRHash(data_concat, pklen);
		cur->pkdata = tab_cell_new(data_concat, pklen);
		cur->prow = row;

		if (i >= TAB_DATA_ROW) {
			struct pk_hash *tmp;
			HASH_FIND(hh_, ret->pkhh, data_concat, pklen, tmp);
			if (NULL == tmp) {
				struct pk_hash *ph = (struct pk_hash *)tab_alloc(sizeof(struct pk_hash) + sizeof(char) * (pklen));
				memcpy(ph->pk, data_concat, pklen);
				ph->trow = row;
				ph->def_idx = i;
				HASH_ADD_KEYPTR(hh_, ret->pkhh, ph->pk, pklen, ph);
			}
			else {
				return NULL;
			}
		}
	}
	return ret;
}

void				tab_kill_primary_key_map(primary_key_map_t *map)
{
	struct pk_hash *key_head = map->pkhh;
	struct pk_hash *cur, *tmp;
	HASH_ITER(hh_, key_head, cur, tmp)
	{
		HASH_DELETE(hh_, key_head, cur);
	}
	for (int i = 0; i < map->nkeys; i++)
	{
		tab_cell_unref(map->keyarr[i].pkdata);
	}
	tab_free(map->pkeyarr);
	tab_free(map->keyarr);
	tab_free(map);
}

primary_key_t	**tab_get_primary_key_array(primary_key_map_t *dict)
{
	return dict->pkeyarr;
}

static unsigned long long cvt2ull(const char *data) {
	unsigned long long retval = 0;
	while (*data) {
		if (data[0] > '9' || data[0] < '0')
			return (unsigned long long) - 1;
		retval = retval * 10 + (data[0] - '0');
		++data;
	}
	return retval;
}

void build_sort_key(const tab_desc_t *tab_source, struct tab_sort_row output[]) {
	for (int i = 0; i < tab_source->npk; i++) {
		int pk_idx = tab_source->pk_idxes[i];
		int flag = 1;
		for (int j = 0; j < TAB_DATA_ROW; j++) {
			struct tab_sort_row *r = &output[j];
			r->prow = tab_source->array[j];
		}
		for (int j = TAB_DATA_ROW; j < tab_source->row; j++)
		{
			struct tab_sort_row *r = &output[j];
			r->npk = tab_source->npk;
			r->prow = tab_source->array[j];
			char *data = (char *)tab_source->array[j][pk_idx]->data;
			r->pkcols[i].data = data;
			unsigned long long cvt = cvt2ull(data);
			if (cvt != (unsigned long long)-1) {
				r->pkcols[i].num = cvt;
			}
			else {
				flag = 0;
			}
		}

		for (int j = TAB_DATA_ROW; j < tab_source->row; j++)
		{
			struct tab_sort_row *r = &output[j];
			if (flag) r->pkcols[i].data = NULL;
		}
	}
}

const tab_cell_t **find_row_by_pk(const primary_key_map_t *pkm, const tab_cell_t *pk) {
	struct pk_hash *res = NULL;
	HASH_FIND(hh_, pkm->pkhh, pk->data, pk->len, res);
	if (NULL != res) {
		return res->trow;
	}
	return NULL;
}

int tab_desc_guess_pk(tab_desc_t *tab_org, int assigned_num, int *assigned_arr) {
	if (tab_org->npk > 0)
		return 0;
	if (assigned_num)
	{
		tab_org->npk = assigned_num;
		for (int i = 0; i < assigned_num; i++)
			tab_org->pk_idxes[i] = assigned_arr[i];
		return 1;
	}

	tab_org->npk = 1;
	tab_org->pk_idxes[0] = 0;
	{
		struct pk_hash *ch = NULL;
		struct pk_hash *res = NULL;
		struct pk_hash *tmp = NULL;

		uint8_t stack_cache[2048];
		//from line 4; i=2
		for (int i = 2; i < tab_org->row; i++) {
			uint8_t *ppk = stack_cache;
			size_t pk_cat_len = 0;
			const tab_cell_t **prow = tab_org->array[i];

			for (int j = 0; j < tab_org->npk; j++) {
				int pk_idx = tab_org->pk_idxes[j];
				pk_cat_len += prow[pk_idx]->len + 1;
			}
			if (pk_cat_len > sizeof(stack_cache))
				ppk = (char *)tab_alloc(pk_cat_len);

			uint8_t *pcur = ppk;
			for (int j = 0; j < tab_org->npk; j++) {
				int pk_idx = tab_org->pk_idxes[j];
				for (uint8_t *pstart = (uint8_t *)prow[pk_idx]->data, *pend = (uint8_t *)prow[pk_idx]->data + prow[pk_idx]->len;
					pstart < pend; *pcur++ = *pstart++);
				*pcur++ = '\0';
			}

			HASH_FIND(hh_, ch, ppk, pk_cat_len, res);
			if (res)
				return 0;
			else {
				tmp = (struct pk_hash *)tab_alloc(sizeof(struct pk_hash) + pk_cat_len);
				tmp->def_idx = i;
				memcpy(tmp->pk, ppk, pk_cat_len);
				HASH_ADD_KEYPTR(hh_, ch, tmp->pk, pk_cat_len, tmp);
			}

			if (ppk != stack_cache)
				tab_free(ppk);
		}
		HASH_ITER(hh_, ch, res, tmp) {
			HASH_DELETE(hh_, ch, res);
			tab_free(res);
		};
	}

	return 1;
}

const tab_bug_t *tab_desc_verify(const tab_desc_t *tab_org) {
	if (tab_org->npk <= 0) {
		return NULL;
	}
	tab_bug_t *retval = tab_alloc(sizeof(tab_bug_t));
	retval->ncol = retval->nrow = retval->ncell_conf = 0;
	retval->col_bug = retval->row_bug = NULL;

	{
		struct colname_hash *ch = NULL;
		struct colname_hash *res = NULL;
		struct colname_hash *tmp = NULL;

		for (int i = 0; i < tab_org->col; i++) {
			HASH_FIND(hh_, ch, tab_org->hdr_cell[i]->data, tab_org->hdr_cell[i]->len, res);
			if (res) {
				retval->ncol++;
				retval->col_bug = (struct tab_dup *)tab_realloc(retval->col_bug, sizeof(struct tab_dup)*retval->ncol);
				retval->col_bug[retval->ncol - 1].cell = tab_org->hdr_cell[i];
				retval->col_bug[retval->ncol - 1].def = res->def_idx;
				retval->col_bug[retval->ncol - 1].dup = i;
			}
			else {
				tmp = (struct colname_hash *)tab_alloc(sizeof(struct colname_hash) + tab_org->hdr_cell[i]->len + 1);
				tmp->def_idx = i;
				memcpy(tmp->name, tab_org->hdr_cell[i]->data, tab_org->hdr_cell[i]->len);
				HASH_ADD_KEYPTR(hh_, ch, tmp->name, tab_org->hdr_cell[i]->len, tmp);
			}
		}

		HASH_ITER(hh_, ch, res, tmp) {
			HASH_DELETE(hh_, ch, res);
			tab_free(res);
		};
	}

	{
		struct pk_hash *ch = NULL;
		struct pk_hash *res = NULL;
		struct pk_hash *tmp = NULL;

		char stack_cache[1024];
		//from line 4; i=2
		for (int i = 2; i < tab_org->row; i++) {
			const tab_cell_t **prow = tab_org->array[i];

			for (int j = 0; j < tab_org->col; j++) {
				if (strstr(prow[j]->data, CELL_CONFLICT_MARK))
					retval->ncell_conf++;
			}

			char *ppk = stack_cache;
			size_t pk_cat_len = 0;
			for (int j = 0; j < tab_org->npk; j++) {
				int pk_idx = tab_org->pk_idxes[j];
				pk_cat_len += prow[pk_idx]->len + 1;
			}
			if (pk_cat_len > sizeof(stack_cache))
				ppk = (char *)tab_alloc(pk_cat_len);
			//assert(ppk);

			char *pcur = ppk;
			for (int j = 0; j < tab_org->npk; j++) {
				int pk_idx = tab_org->pk_idxes[j];
				for (const uint8_t *pstart = prow[pk_idx]->data, *pend = prow[pk_idx]->data + prow[pk_idx]->len;
					pstart < pend; *pcur++ = *pstart++);
				*pcur++ = '#';
			}
			pcur[-1] = '\0';

			HASH_FIND(hh_, ch, ppk, pk_cat_len, res);
			if (res) {
				retval->nrow++;
				retval->row_bug = (struct tab_dup *)tab_realloc(retval->row_bug, sizeof(struct tab_dup)*retval->nrow);
				retval->row_bug[retval->nrow - 1].cell = tab_cell_new(ppk, pk_cat_len);
				retval->row_bug[retval->nrow - 1].def = res->def_idx;
				retval->row_bug[retval->nrow - 1].dup = i;
			}
			else {
				tmp = (struct pk_hash *)tab_alloc(sizeof(struct pk_hash) + pk_cat_len);
				tmp->def_idx = i;
				memcpy(tmp->pk, ppk, pk_cat_len);
				HASH_ADD_KEYPTR(hh_, ch, tmp->pk, pk_cat_len, tmp);
			}

			if (ppk != stack_cache)
				tab_free(ppk);
		}
		HASH_ITER(hh_, ch, res, tmp) {
			HASH_DELETE(hh_, ch, res);
			tab_free(res);
		};
	}

	if (retval->ncol == 0 && retval->nrow == 0 && retval->ncell_conf == 0)
		return NULL;

	return retval;
}

const tab_desc_t *tab_desc_sort(const tab_desc_t *tab_old, const char *order) {
	int(*pcmp)(const struct tab_sort_row *pka, const struct tab_sort_row *pkb) = NULL;

	if (tab_old->npk <= 0)
		return NULL;

	if (0 == strcmp(order, "asc"))
		pcmp = pk_cmp_asc;
	else if (0 == strcmp(order, "dsc"))
		pcmp = pk_cmp_dsc;
	if (NULL == pcmp)
		return NULL;

	tab_desc_t *retval = (tab_desc_t *)tab_alloc(sizeof(tab_desc_t));
	retval->hdr_cell = (tab_cell_t **)tab_alloc(sizeof(tab_cell_t *)*tab_old->col);
	retval->array = (tab_cell_t ***)tab_alloc(sizeof(tab_cell_t **)*tab_old->row);

	struct tab_sort_row *output = (struct tab_sort_row *)tab_alloc(sizeof(struct tab_sort_row)*tab_old->row);
	build_sort_key(tab_old, output);
	qsort(output + TAB_DATA_ROW, tab_old->row - TAB_DATA_ROW, sizeof(struct tab_sort_row), pcmp);

	for (int j = 0; j < tab_old->col; j++)
	{
		retval->hdr_cell[j] = tab_cell_ref(tab_old->hdr_cell[j]);
	}
	for (int i = 0; i < tab_old->row; i++) {
		retval->array[i] = tab_alloc(sizeof(void *) * tab_old->col);
		for (int j = 0; j < tab_old->col; j++)
		{
			retval->array[i][j] = tab_cell_ref(output[i].prow[j]);
		}
	}

	tab_free(output);
	return retval;
}

int tab_desc_write_invalid(const char *merged_name) {
	FILE *fp = fopen(merged_name, "wb");
	if (NULL == fp)
		return -1;
	fprintf(fp, "@there are conflicts\n");
	fclose(fp);
	return 0;
}

int tab_desc_check_same_pk2(const tab_desc_t *a, const tab_desc_t *b)  {
	char pka[1024];
	char pkb[1024];
	size_t len_a;
	size_t len_b;

	assert(a->npk <= PKMAX);
	assert(b->npk <= PKMAX);

	len_a = fill_pk(a, a->hdr_cell, pka);
	len_b = fill_pk(b, b->hdr_cell, pkb);
	if (len_a != len_b) return 0;
	if (memcmp(pka, pkb, len_a)) return 0;
	return 1;
}

int tab_desc_check_same_pk3(const tab_desc_t *a, const tab_desc_t *b, const tab_desc_t *c)  {
	char pka[1024];
	char pkb[1024];
	char pkc[1024];
	size_t len_a;
	size_t len_b;
	size_t len_c;

	assert(a->npk <= PKMAX);
	assert(b->npk <= PKMAX);
	assert(c->npk <= PKMAX);

	len_a = fill_pk(a, a->hdr_cell, pka);
	len_b = fill_pk(b, b->hdr_cell, pkb);
	if (len_a != len_b) return 0;
	if (memcmp(pka, pkb, len_a)) return 0;

	len_c = fill_pk(c, c->hdr_cell, pkc);
	if (len_a != len_c) return 0;
	if (memcmp(pka, pkc, len_c)) return 0;

	return 1;
}

const char *sharp_split_whole_pk(tab_cell_t *pc) {
	static char tmp[2048];
	char *ps = pc->data;
	char *pss = tmp;
	char *pe = pc->data + pc->len - 1;
	for (; ps < pe; ps++, pss++) { *pss = ps[0] ? ps[0] : '#'; }
	return tmp;
}

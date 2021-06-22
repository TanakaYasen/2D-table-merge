//ld.c
//get the Levenshtein Distance / Edit Distance
// http://www.cnblogs.com/grenet/archive/2010/06/01/1748448.html

#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include "core_algorithm.h"

#define algo_alloc	malloc
#define algo_free	free
#define MATRIX_REF(ldd, i, j, pitch)	ldd[(j)*(pitch+1)+(i)]

int ld_fast_of(void *s1[], int sa, void *s2[], int sb, f_cmp pcmp) {
	unsigned short *dis = algo_alloc(sizeof(short)*(sb + 1));
	int ret;
	sa++;	sb++;

	for (int j = 0; j < sb; j++)
		dis[j] = j;

	for (int i = 1; i < sa; i++) {
		int old = i - 1;
		dis[0] = i;
		for (int j = 1; j < sb; j++) {
			int temp = dis[j];
			{
				int u = dis[j] + 1;
				int v = dis[j-1] + 1;
				int min = u > v ? v : u;
				if (min < old) {
					dis[j] = min;
				} else {
					int w = old;
					if (pcmp(s1[i - 1], s2[j - 1]) != 0)
						w++;
					dis[j] = min > w ? w : min;
				}
			}
			old = temp;
		}
	}
	ret = dis[sb-1];
	algo_free(dis);
	return ret;
}


int ld_path_of(void *s1[], int sa, void *s2[], int sb, f_cmp pcmp, int path[], int *nstep)
{
	typedef struct {
		unsigned short ld;
		unsigned char dir_up:2;
		unsigned char dir_left:2;
		unsigned char dir:4;
	}ed_st_t;

	ed_st_t *ldd = algo_alloc(sizeof(ed_st_t)*(sb + 1)*(sa + 1));
	int ret;

	for (int i = 0; i <= sa; i++) {
		ed_st_t *tmp = &MATRIX_REF(ldd, i, 0, sa);
		tmp->ld = i;
		tmp->dir_left = 1;
		tmp->dir_up = 0;
		tmp->dir = DIR_U;
	}
	for (int j = 0; j <= sb; j++) {
		ed_st_t *tmp = &MATRIX_REF(ldd, 0, j, sa);
		tmp->ld = j;
		tmp->dir_left = 0;
		tmp->dir_up = 1;
		tmp->dir = DIR_L;
	}

	for (int i = 1; i <= sa; i++)
		for (int j = 1; j <= sb; j++) {
			ed_st_t *dst = &MATRIX_REF(ldd, i, j, sa);
			ed_st_t *s1del = &MATRIX_REF(ldd, i - 1, j, sa);
			ed_st_t *s2add = &MATRIX_REF(ldd, i, j - 1, sa);
			ed_st_t *both = &MATRIX_REF(ldd, i - 1, j - 1, sa);
			unsigned short diag = both->ld;

			if (s1del->ld < s2add->ld) {
				dst->ld = s1del->ld + 1;
				dst->dir_left = 1;
				dst->dir_up = 0;
				dst->dir = DIR_U;
			}
			else {
				dst->ld = s2add->ld + 1;
				dst->dir_left = 0;
				dst->dir_up = 1;
				dst->dir = DIR_L;
			}

			if (pcmp(s1[i-1], s2[j-1]) != 0) {//if (a == b)
				diag++; //替换s1[i]->s2[j]
			};
			if (diag < dst->ld) {
				dst->ld = diag;
				dst->dir_left = 1;
				dst->dir_up = 1;
				dst->dir = diag == both->ld ? DIR_LU : DIR_LUX;
			}
		}

	int steps = 0;
	int i = sa, j = sb;
	for (ed_st_t *tmp = &MATRIX_REF(ldd, sa, sb, sa); tmp != &MATRIX_REF(ldd, 0, 0, sa);)	{
		path[steps++] = tmp->dir;
		i -= tmp->dir_left;
		j -= tmp->dir_up;
		tmp = &MATRIX_REF(ldd, i, j, sa);
	}
	*nstep = steps;

	ret = MATRIX_REF(ldd, sa - 1, sb - 1, sa).ld;
	algo_free(ldd);
	return ret;
}

int lcs_fast_of(void *s1[], int sa, void *s2[], int sb, f_cmp pcmp) {
	int i, j;
	unsigned short *ldd = algo_alloc(sizeof(short)*(sb + 1)*(sa + 1));
	int ret;

	for (i = 0; i < sa + 1; ++i)
		for (j = 0; j < sb + 1; ++j)
			if (i == 0 || j == 0)
				MATRIX_REF(ldd, i, j, sa) = 0;
			else if (0 == pcmp(s1[i - 1], s2[j - 1]))
				MATRIX_REF(ldd, i, j, sa) = MATRIX_REF(ldd, i - 1, j - 1, sa) + 1;
			else
			{
				int u = MATRIX_REF(ldd, i - 1, j, sa);
				int v = MATRIX_REF(ldd, i, j - 1, sa);
				MATRIX_REF(ldd, i, j, sa) = u > v ? u : v;
			}

	ret = MATRIX_REF(ldd, sa, sb, sa);
	algo_free(ldd);
	return ret;
}

static int lcs_match_matrix(void *s1[], int sa, void *s2[], int sb, f_cmp pcmp, unsigned char *m) {
	int i, j;
	int ret;
	unsigned short *ldd = algo_alloc(sizeof(short)*(sb + 1)*(sa + 1));

	for (i = 0; i < sa + 1; ++i)
		for (j = 0; j < sb + 1; ++j)
		{
			if (i == 0 || j == 0)
				MATRIX_REF(ldd, i, j, sa) = 0;
			else if (0 == pcmp(s1[i - 1], s2[j - 1]))
				MATRIX_REF(ldd, i, j, sa) = MATRIX_REF(ldd, i - 1, j - 1, sa) + 1;
			else
			{
				int u = MATRIX_REF(ldd, i - 1, j, sa);
				int v = MATRIX_REF(ldd, i, j - 1, sa);
				MATRIX_REF(ldd, i, j, sa) = u > v ? u : v;
			}
		}

	for (i = sa, j = sb; i > 0 && j > 0;){
		if (0 == pcmp(s1[i - 1], s2[j - 1])) {
			MATRIX_REF(m, i, j, sa) = 1;
			i--;
			j--;
		}
		else {
			int u = MATRIX_REF(ldd, i - 1, j, sa);
			int v = MATRIX_REF(ldd, i, j - 1, sa);
			if (u > v) {
				i--;
			}
			else {
				j--;
			}
		}
	}
	ret = MATRIX_REF(ldd, sa, sb, sa);
	algo_free(ldd);

	return ret;
}

// http://www.cis.upenn.edu/~bcpierce/papers/diff3-short.pdf
int match_max_chunks(void *p[], int sp, void *s1[], int sa, void *s2, int sb, f_cmp pcmp, struct tuple *chunks) {
	unsigned char *MatchMatAO = algo_alloc(sizeof(unsigned char)*(sp+1)*(sa+1));
	unsigned char *MatchMatBO = algo_alloc(sizeof(unsigned char)*(sp+1)*(sb+1));
	memset(MatchMatAO, 0, sizeof(unsigned char)*(sp+1)*(sa+1));
	memset(MatchMatBO, 0, sizeof(unsigned char)*(sp+1)*(sb+1));

	lcs_match_matrix(p, sp, s1, sa, pcmp, MatchMatAO);
	lcs_match_matrix(p, sp, s2, sb, pcmp, MatchMatBO);

	int step = 0;
	int lo = 0, la = 0, lb = 0;
	for (;;) {
		int i = 0;
__step2:
		i = 1;
		for (; i <= sp-lo && i <= sa-la && i <= sb-lb; i++)
			if (MATRIX_REF(MatchMatAO, lo + i, la + i, sp) == 0 || MATRIX_REF(MatchMatBO, lo + i, lb + i, sp) == 0) {
				goto __least_i_;
			}
		{
			struct tuple *pc = &chunks[step++];
			pc->stable = 1;
			pc->a.from = la + 1;
			pc->a.to = la += i - 1;
			pc->o.from = lo + 1;
			pc->o.to = lo += i - 1;
			pc->b.from = lb + 1;
			pc->b.to = lb += i - 1;
		}
		break; //stable
	__least_i_:
		if (i == 1) {
			for (int o = lo + 1; o <= sp; o++) {
				int a = 0, b = 0;
				//find least dual match
				for (int a = la + 1; a <= sa; a++)
					if (MATRIX_REF(MatchMatAO, o, a, sp))
						for (b = lb + 1; b <= sb; b++)
							if (MATRIX_REF(MatchMatBO, o, b, sp)) {
								//output unstable C={[la+1, a-1], [lo+1, o−1], [lb+1, b−1]}
								struct tuple *pc = &chunks[step++];
								pc->stable = 0;
								pc->a.from = la + 1;
								pc->a.to = a - 1;
								pc->o.from = lo + 1;
								pc->o.to = o - 1;
								pc->b.from = lb + 1;
								pc->b.to = b - 1;
								lo = o - 1;
								la = a - 1;
								lb = b - 1;
								goto __step2;
							} 
			}
			break; //unstable;
		}
		else {
			// output stable: C = ([ℓA + 1 .. ℓA + i − 1], [ℓO + 1 .. ℓO + i − 1], [ℓB + 1 .. ℓB + i − 1])
			struct tuple *pc = &chunks[step++];
			pc->stable = 1;
			pc->a.from = la + 1;
			pc->a.to = la += i - 1;
			pc->o.from = lo + 1;
			pc->o.to = lo += i - 1;
			pc->b.from = lb + 1;
			pc->b.to = lb += i - 1;
		}
	}
	//output final
	if (lo < sp || la < sa || lb < sb) {
		//output C = ([ℓA + 1 .. | A | ], [ℓO + 1 .. | O | ], [ℓB + 1 .. | B | ])
		struct tuple *pc = &chunks[step++];
		pc->stable = 0;
		pc->a.from = la + 1;
		pc->a.to = sa;
		pc->o.from = lo + 1;
		pc->o.to = sp;
		pc->b.from = lb + 1;
		pc->b.to = sb;
	}

	for (int i = 0; i < step; ++i) {
		struct tuple *pc = &chunks[i];
		pc->a.from--;
		pc->a.to--;
		pc->o.from--;
		pc->o.to--;
		pc->b.from--;
		pc->b.to--;
	}

	algo_free(MatchMatAO);
	algo_free(MatchMatBO);
	return step;
}


double similarity_of(void *s1[], int sa, void *s2[], int sb, f_cmp pcmp) {
	double ld = (double)ld_fast_of(s1, sa, s2, sb, pcmp);
	double lcs = (double)lcs_fast_of(s1, sa, s2, sb, pcmp);
	return (lcs) / (lcs + ld);
}
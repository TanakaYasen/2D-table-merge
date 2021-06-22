#ifndef _ALGORITHM_H
#define _ALGORITHM_H

struct pair{
	short from;
	short to;
};
struct tuple {
	int stable;
	struct pair a, o, b;
};

#define DIR_LU 0
#define DIR_LUX 1
#define DIR_U 2  // + s2[j]
#define DIR_L 3  // - s1[i] 

typedef int(*f_cmp)(void *a, void *b); //return 0 if a equals b and vice versus.
int ld_fast_of(void *s1[], int sa, void *s2[], int sb, f_cmp pcmp);
int ld_path_of(void *s1[], int sa, void *s2[], int sb, f_cmp pcmp, int path[], int *nstep);

int lcs_fast_of(void *s1[], int sa, void *s2[], int sb, f_cmp pcmp);

int match_max_chunks(void *p[], int sp, void *s1[], int sa, void *s2, int sb, f_cmp pcmp, struct tuple *);
 //return value in [0, 1.0],  1 for absolutely equal; 0 for totally different. AND ensure sim(A,B) = sim(B, A)
double similarity_of(void *s1[], int sa, void *s2[], int sb, f_cmp pcmp);
#endif
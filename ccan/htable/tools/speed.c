/* Simple speed tests for hashtables. */
#include <ccan/htable/htable_type.h>
#include <ccan/htable/htable.c>
#include <ccan/hash/hash.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

static size_t hashcount;
struct object {
	/* The key. */
	unsigned int key;

	/* Some contents. Doubles as consistency check. */
	struct object *self;
};

static const unsigned int *objkey(const struct object *obj)
{
	return &obj->key;
}

static size_t hash_obj(const unsigned int *key)
{
	hashcount++;
	return hashl(key, 1, 0);
}

static bool cmp(const struct object *object, const unsigned int *key)
{
	return object->key == *key;
}

HTABLE_DEFINE_TYPE(struct object, objkey, hash_obj, cmp, obj);

static unsigned int popcount(unsigned long val)
{
#if HAVE_BUILTIN_POPCOUNTL
	return __builtin_popcountl(val);
#else
	if (sizeof(long) == sizeof(u64)) {
		u64 v = val;
		v = (v & 0x5555555555555555ULL)
			+ ((v >> 1) & 0x5555555555555555ULL);
		v = (v & 0x3333333333333333ULL)
			+ ((v >> 1) & 0x3333333333333333ULL);
		v = (v & 0x0F0F0F0F0F0F0F0FULL)
			+ ((v >> 1) & 0x0F0F0F0F0F0F0F0FULL);
		v = (v & 0x00FF00FF00FF00FFULL)
			+ ((v >> 1) & 0x00FF00FF00FF00FFULL);
		v = (v & 0x0000FFFF0000FFFFULL)
			+ ((v >> 1) & 0x0000FFFF0000FFFFULL);
		v = (v & 0x00000000FFFFFFFFULL)
			+ ((v >> 1) & 0x00000000FFFFFFFFULL);
		return v;
	}
	val = (val & 0x55555555ULL) + ((val >> 1) & 0x55555555ULL);
	val = (val & 0x33333333ULL) + ((val >> 1) & 0x33333333ULL);
	val = (val & 0x0F0F0F0FULL) + ((val >> 1) & 0x0F0F0F0FULL);
	val = (val & 0x00FF00FFULL) + ((val >> 1) & 0x00FF00FFULL);
	val = (val & 0x0000FFFFULL) + ((val >> 1) & 0x0000FFFFULL);
	return val;
#endif
}

static size_t perfect(const struct htable *ht)
{
	size_t i, placed_perfect = 0;

	for (i = 0; i < ((size_t)1 << ht->bits); i++) {
		if (!entry_is_valid(ht->table[i]))
			continue;
		if (hash_bucket(ht, ht->rehash(get_raw_ptr(ht, ht->table[i]),
					       ht->priv)) == i) {
			assert((ht->table[i] & ht->perfect_bit)
			       == ht->perfect_bit);
			placed_perfect++;
		}
	}
	return placed_perfect;
}

static size_t count_deleted(const struct htable *ht)
{
	size_t i, delete_markers = 0;

	for (i = 0; i < ((size_t)1 << ht->bits); i++) {
		if (ht->table[i] == HTABLE_DELETED)
			delete_markers++;
	}
	return delete_markers;
}

/* Nanoseconds per operation */
static size_t normalize(const struct timeval *start,
			const struct timeval *stop,
			unsigned int num)
{
	struct timeval diff;

	timersub(stop, start, &diff);

	/* Floating point is more accurate here. */
	return (double)(diff.tv_sec * 1000000 + diff.tv_usec)
		/ num * 1000;
}

static size_t worst_run(struct htable *ht, size_t *deleted)
{
	size_t longest = 0, len = 0, this_del = 0, i;

	*deleted = 0;
	/* This doesn't take into account end-wrap, but gives an idea. */
	for (i = 0; i < ((size_t)1 << ht->bits); i++) {
		if (ht->table[i]) {
			len++;
			if (ht->table[i] == HTABLE_DELETED)
				this_del++;
		} else {
			if (len > longest) {
				longest = len;
				*deleted = this_del;
			}
			len = 0;
			this_del = 0;
		}
	}
	return longest;
}

int main(int argc, char *argv[])
{
	struct object *objs;
	size_t i, j, num, deleted;
	struct timeval start, stop;
	struct htable_obj *ht;
	struct htable *htr;
	bool make_dumb = false;

	if (argv[1] && strcmp(argv[1], "--dumb") == 0) {
		argv++;
		make_dumb = true;
	}
	num = argv[1] ? atoi(argv[1]) : 1000000;
	objs = calloc(num, sizeof(objs[0]));

	for (i = 0; i < num; i++) {
		objs[i].key = i;
		objs[i].self = &objs[i];
	}

	ht = htable_obj_new();
	htr = (void *)ht;

	printf("Initial insert: ");
	fflush(stdout);
	gettimeofday(&start, NULL);
	for (i = 0; i < num; i++)
		htable_obj_add(ht, objs[i].self);
	gettimeofday(&stop, NULL);
	printf(" %zu ns\n", normalize(&start, &stop, num));
	printf("Details: hash size %u, mask bits %u, perfect %.0f%%\n",
	       1U << htr->bits, popcount(htr->common_mask),
	       perfect(htr) * 100.0 / htr->elems);

	if (make_dumb) {
		/* Screw with mask, to hobble us. */
		update_common(htr, (void *)~htr->common_bits);
		printf("Details: DUMB MODE: mask bits %u\n",
		       popcount(htr->common_mask));
	}

	printf("Initial lookup (match): ");
	fflush(stdout);
	gettimeofday(&start, NULL);
	for (i = 0; i < num; i++)
		if (htable_obj_get(ht, &i)->self != objs[i].self)
			abort();
	gettimeofday(&stop, NULL);
	printf(" %zu ns\n", normalize(&start, &stop, num));

	printf("Initial lookup (miss): ");
	fflush(stdout);
	gettimeofday(&start, NULL);
	for (i = 0; i < num; i++) {
		unsigned int n = i + num;
		if (htable_obj_get(ht, &n))
			abort();
	}
	gettimeofday(&stop, NULL);
	printf(" %zu ns\n", normalize(&start, &stop, num));

	/* Lookups in order are very cache-friendly for judy; try random */
	printf("Initial lookup (random): ");
	fflush(stdout);
	gettimeofday(&start, NULL);
	for (i = 0, j = 0; i < num; i++, j = (j + 10007) % num)
		if (htable_obj_get(ht, &j)->self != &objs[j])
			abort();
	gettimeofday(&stop, NULL);
	printf(" %zu ns\n", normalize(&start, &stop, num));

	hashcount = 0;
	printf("Initial delete all: ");
	fflush(stdout);
	gettimeofday(&start, NULL);
	for (i = 0; i < num; i++)
		if (!htable_obj_del(ht, objs[i].self))
			abort();
	gettimeofday(&stop, NULL);
	printf(" %zu ns\n", normalize(&start, &stop, num));
	printf("Details: rehashes %zu\n", hashcount);

	printf("Initial re-inserting: ");
	fflush(stdout);
	gettimeofday(&start, NULL);
	for (i = 0; i < num; i++)
		htable_obj_add(ht, objs[i].self);
	gettimeofday(&stop, NULL);
	printf(" %zu ns\n", normalize(&start, &stop, num));

	hashcount = 0;
	printf("Deleting first half: ");
	fflush(stdout);
	gettimeofday(&start, NULL);
	for (i = 0; i < num; i+=2)
		if (!htable_obj_del(ht, objs[i].self))
			abort();
	gettimeofday(&stop, NULL);
	printf(" %zu ns\n", normalize(&start, &stop, num));

	printf("Details: rehashes %zu, delete markers %zu\n",
	       hashcount, count_deleted(htr));

	printf("Adding (a different) half: ");
	fflush(stdout);

	for (i = 0; i < num; i+=2)
		objs[i].key = num+i;

	gettimeofday(&start, NULL);
	for (i = 0; i < num; i+=2)
		htable_obj_add(ht, objs[i].self);
	gettimeofday(&stop, NULL);
	printf(" %zu ns\n", normalize(&start, &stop, num));

	printf("Details: delete markers %zu, perfect %.0f%%\n",
	       count_deleted(htr), perfect(htr) * 100.0 / htr->elems);

	printf("Lookup after half-change (match): ");
	fflush(stdout);
	gettimeofday(&start, NULL);
	for (i = 1; i < num; i+=2)
		if (htable_obj_get(ht, &i)->self != objs[i].self)
			abort();
	for (i = 0; i < num; i+=2) {
		unsigned int n = i + num;
		if (htable_obj_get(ht, &n)->self != objs[i].self)
			abort();
	}
	gettimeofday(&stop, NULL);
	printf(" %zu ns\n", normalize(&start, &stop, num));

	printf("Lookup after half-change (miss): ");
	fflush(stdout);
	gettimeofday(&start, NULL);
	for (i = 0; i < num; i++) {
		unsigned int n = i + num * 2;
		if (htable_obj_get(ht, &n))
			abort();
	}
	gettimeofday(&stop, NULL);
	printf(" %zu ns\n", normalize(&start, &stop, num));

	/* Hashtables with delete markers can fill with markers over time.
	 * so do some changes to see how it operates in long-term. */
	for (i = 0; i < 5; i++) {
		if (i == 0) {
			/* We don't measure this: jmap is different. */
			printf("Details: initial churn\n");
		} else {
			printf("Churning %s time: ",
			       i == 1 ? "second"
			       : i == 2 ? "third"
			       : i == 3 ? "fourth"
			       : "fifth");
			fflush(stdout);
		}
		gettimeofday(&start, NULL);
		for (j = 0; j < num; j++) {
			if (!htable_obj_del(ht, &objs[j]))
				abort();
			objs[j].key = num*i+j;
			if (!htable_obj_add(ht, &objs[j]))
				abort();
		}
		gettimeofday(&stop, NULL);
		if (i != 0)
			printf(" %zu ns\n", normalize(&start, &stop, num));
	}

	/* Spread out the keys more to try to make it harder. */
	printf("Details: reinserting with spread\n");
	for (i = 0; i < num; i++) {
		if (!htable_obj_del(ht, objs[i].self))
			abort();
		objs[i].key = num * 5 + i * 9;
		if (!htable_obj_add(ht, objs[i].self))
			abort();
	}
	printf("Details: delete markers %zu, perfect %.0f%%\n",
	       count_deleted(htr), perfect(htr) * 100.0 / htr->elems);
	i = worst_run(htr, &deleted);
	printf("Details: worst run %zu (%zu deleted)\n", i, deleted);

	printf("Lookup after churn & spread (match): ");
	fflush(stdout);
	gettimeofday(&start, NULL);
	for (i = 0; i < num; i++) {
		unsigned int n = num * 5 + i * 9;
		if (htable_obj_get(ht, &n)->self != objs[i].self)
			abort();
	}
	gettimeofday(&stop, NULL);
	printf(" %zu ns\n", normalize(&start, &stop, num));

	printf("Lookup after churn & spread (miss): ");
	fflush(stdout);
	gettimeofday(&start, NULL);
	for (i = 0; i < num; i++) {
		unsigned int n = num * (5 + 9) + i * 9;
		if (htable_obj_get(ht, &n))
			abort();
	}
	gettimeofday(&stop, NULL);
	printf(" %zu ns\n", normalize(&start, &stop, num));

	printf("Lookup after churn & spread (random): ");
	fflush(stdout);
	gettimeofday(&start, NULL);
	for (i = 0, j = 0; i < num; i++, j = (j + 10007) % num) {
		unsigned int n = num * 5 + j * 9;
		if (htable_obj_get(ht, &n)->self != &objs[j])
			abort();
	}
	gettimeofday(&stop, NULL);
	printf(" %zu ns\n", normalize(&start, &stop, num));

	hashcount = 0;
	printf("Deleting half after churn & spread: ");
	fflush(stdout);
	gettimeofday(&start, NULL);
	for (i = 0; i < num; i+=2)
		if (!htable_obj_del(ht, objs[i].self))
			abort();
	gettimeofday(&stop, NULL);
	printf(" %zu ns\n", normalize(&start, &stop, num));

	printf("Adding (a different) half after churn & spread: ");
	fflush(stdout);

	for (i = 0; i < num; i+=2)
		objs[i].key = num*6+i*9;

	gettimeofday(&start, NULL);
	for (i = 0; i < num; i+=2)
		htable_obj_add(ht, objs[i].self);
	gettimeofday(&stop, NULL);
	printf(" %zu ns\n", normalize(&start, &stop, num));

	printf("Details: delete markers %zu, perfect %.0f%%\n",
	       count_deleted(htr), perfect(htr) * 100.0 / htr->elems);

	return 0;
}

/* GNU LGPL version 2 (or later) - see LICENSE file for details */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>

#include <ccan/ptrint/ptrint.h>
#include <ccan/compiler/compiler.h>
#include <ccan/build_assert/build_assert.h>
#include <ccan/coroutine/coroutine.h>

/*
 * Stack management
 */

/* Returns lowest stack addres, regardless of growth direction */
static UNNEEDED void *coroutine_stack_base(struct coroutine_stack *stack)
{
#if HAVE_STACK_GROWS_UPWARDS
	return (char *)(stack + 1);
#else
	return (char *)stack - stack->size;
#endif
}

#if HAVE_VALGRIND_MEMCHECK_H
#include <valgrind/memcheck.h>
static void vg_register_stack(struct coroutine_stack *stack)
{
	char *base = coroutine_stack_base(stack);

	VALGRIND_MAKE_MEM_UNDEFINED(base, stack->size);
	stack->valgrind_id = VALGRIND_STACK_REGISTER(base,
						     base + stack->size - 1);
}

static void vg_deregister_stack(struct coroutine_stack *stack)
{
	VALGRIND_MAKE_MEM_UNDEFINED(coroutine_stack_base(stack), stack->size);
	VALGRIND_STACK_DEREGISTER(stack->valgrind_id);
}
static bool vg_addressable(void *p, size_t len)
{
	return !VALGRIND_CHECK_MEM_IS_ADDRESSABLE(p, len);
}
#else
#define vg_register_stack(stack)		do { } while (0)
#define vg_deregister_stack(stack)		do { } while (0)
#define vg_addressable(p, len)			(true)
#endif

struct coroutine_stack *coroutine_stack_init(void *buf, size_t bufsize,
					     size_t metasize)
{
	struct coroutine_stack *stack;
	size_t size = bufsize - sizeof(*stack) - metasize;

#ifdef MINSIGSTKSZ
	BUILD_ASSERT(COROUTINE_MIN_STKSZ >= MINSIGSTKSZ);
#endif

	if (bufsize < (COROUTINE_MIN_STKSZ + sizeof(*stack) + metasize))
		return NULL;

#if HAVE_STACK_GROWS_UPWARDS
	stack = (char *)buf + metasize;
#else
	stack = (struct coroutine_stack *)
		((char *)buf + bufsize - metasize) - 1;
#endif

	stack->magic = COROUTINE_STACK_MAGIC;
	stack->size = size;
	vg_register_stack(stack);
	return stack;
}

void coroutine_stack_release(struct coroutine_stack *stack, size_t metasize)
{
	vg_deregister_stack(stack);
	memset(stack, 0, sizeof(*stack));
}

struct coroutine_stack *coroutine_stack_check(struct coroutine_stack *stack,
					      const char *abortstr)
{
	if (stack && vg_addressable(stack, sizeof(*stack))
	    && (stack->magic == COROUTINE_STACK_MAGIC)
	    && (stack->size >= COROUTINE_MIN_STKSZ))
		return stack;

	if (abortstr) {
		if (!stack)
			fprintf(stderr, "%s: NULL coroutine stack\n", abortstr);
		else
			fprintf(stderr,
				"%s: Bad coroutine stack at %p (magic=0x%"PRIx64" size=%zd)\n",
				abortstr, stack, stack->magic, stack->size);
		abort();
	}
	return NULL;
}

size_t coroutine_stack_size(const struct coroutine_stack *stack)
{
	return stack->size;
}

#if HAVE_UCONTEXT
static void coroutine_uc_stack(stack_t *uc_stack,
			       const struct coroutine_stack *stack)
{
	uc_stack->ss_size = coroutine_stack_size(stack);
	uc_stack->ss_sp = coroutine_stack_base((struct coroutine_stack *)stack);
}
#endif /* HAVE_UCONTEXT */

/*
 * Coroutine switching
 */

#if HAVE_UCONTEXT
void coroutine_init_(struct coroutine_state *cs,
		     void (*fn)(void *), void *arg,
		     struct coroutine_stack *stack)
{
	getcontext (&cs->uc);

	coroutine_uc_stack(&cs->uc.uc_stack, stack);

        if (HAVE_POINTER_SAFE_MAKECONTEXT) {
                makecontext(&cs->uc, (void *)fn, 1, arg);
        } else {
                ptrdiff_t si = ptr2int(arg);
                ptrdiff_t mask = (1UL << (sizeof(int) * 8)) - 1;
                int lo = si & mask;
                int hi = si >> (sizeof(int) * 8);

                makecontext(&cs->uc, (void *)fn, 2, lo, hi);
        }
	
}

void coroutine_jump(const struct coroutine_state *to)
{
	setcontext(&to->uc);
	assert(0);
}

void coroutine_switch(struct coroutine_state *from,
		      const struct coroutine_state *to)
{
	int rc;

	rc = swapcontext(&from->uc, &to->uc);
	assert(rc == 0);
}
#endif /* HAVE_UCONTEXT */

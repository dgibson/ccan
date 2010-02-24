#include <tools/ccanlint/ccanlint.h>
#include <tools/tools.h>
#include <ccan/talloc/talloc.h>
#include <ccan/str/str.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <string.h>
#include <ctype.h>

static const char *can_run(struct manifest *m)
{
	if (safe_mode)
		return "Safe mode enabled";
	return NULL;
}

struct run_tests_result {
	struct list_node list;
	struct ccan_file *file;
	const char *output;
};

static void *do_run_tests(struct manifest *m)
{
	struct list_head *list = talloc(m, struct list_head);
	struct run_tests_result *res;
	struct ccan_file *i;
	char *cmdout;
	char *olddir;

	/* We run tests in the module directory, so any paths
	 * referenced can all be module-local. */
	olddir = talloc_getcwd(m);
	if (!olddir)
		err(1, "Could not save cwd");
	if (chdir(m->dir) != 0)
		err(1, "Could not chdir to %s", m->dir);

	list_head_init(list);

	list_for_each(&m->run_tests, i, list) {
		run_tests.total_score++;
		/* FIXME: timeout here */
		cmdout = run_command(m, i->compiled);
		if (cmdout) {
			res = talloc(list, struct run_tests_result);
			res->file = i;
			res->output = talloc_steal(res, cmdout);
			list_add_tail(list, &res->list);
		}
	}

	list_for_each(&m->api_tests, i, list) {
		run_tests.total_score++;
		/* FIXME: timeout here */
		cmdout = run_command(m, i->compiled);
		if (cmdout) {
			res = talloc(list, struct run_tests_result);
			res->file = i;
			res->output = talloc_steal(res, cmdout);
			list_add_tail(list, &res->list);
		}
	}

	if (list_empty(list)) {
		talloc_free(list);
		list = NULL;
	}

	if (chdir(olddir) != 0)
		err(1, "Could not chdir to %s", olddir);

	return list;
}

static unsigned int score_run_tests(struct manifest *m, void *check_result)
{
	struct list_head *list = check_result;
	struct run_tests_result *i;
	unsigned int score = run_tests.total_score;

	list_for_each(list, i, list)
		score--;
	return score;
}

static const char *describe_run_tests(struct manifest *m,
					  void *check_result)
{
	struct list_head *list = check_result;
	char *descrip = talloc_strdup(check_result, "Running tests failed:\n");
	struct run_tests_result *i;

	list_for_each(list, i, list)
		descrip = talloc_asprintf_append(descrip, "Running %s:\n%s",
						 i->file->name, i->output);
	return descrip;
}

/* Gcc's warn_unused_result is fascist bullshit. */
#define doesnt_matter()

static void run_under_debugger(struct manifest *m, void *check_result)
{
	char *command;
	struct list_head *list = check_result;
	struct run_tests_result *first;

	if (!ask("Should I run the first failing test under the debugger?"))
		return;

	first = list_top(list, struct run_tests_result, list);
	command = talloc_asprintf(m, "gdb -ex 'break tap.c:136' -ex 'run' %s",
				  first->file->compiled);
	if (system(command))
		doesnt_matter();
}

struct ccanlint run_tests = {
	.name = "run and api tests run successfully",
	.score = score_run_tests,
	.check = do_run_tests,
	.describe = describe_run_tests,
	.can_run = can_run,
	.handle = run_under_debugger
};

REGISTER_TEST(run_tests, &compile_tests, NULL);

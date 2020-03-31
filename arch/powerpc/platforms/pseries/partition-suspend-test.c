// SPDX-License-Identifier: GPL-2.0
/*
 * Tests for code related to the Partition Suspension Option as
 * specified in the Power Architecture® Platform Requirements+.
 */

#include <kunit/test.h>

#include "papr-suspend.h"

#define TEST_VASI_STREAM_ID 0xabcdabcdabcdabcd

/* VASI Suspend State Sequence paster macro */
#define V3S(name, first_value, ...)			\
	vasi_suspend_state_t name[] = {			\
		[0] = (first_value),			\
		##__VA_ARGS__,				\
		VASI_SUSPEND_STATE_TEST_SEQ_END,	\
	}

static const V3S(invalid_at_start, VASI_SUSPEND_STATE_INVALID);

struct suspend_test_context {
	struct papr_lpar_suspend_session session;
	const vasi_suspend_state_t *state_seq;
	unsigned short state_seqno;
	struct kunit *test;
};

static vasi_suspend_state_t test_poll_vasi_state(struct papr_lpar_suspend_session *s)
{
	struct suspend_test_context *ctx;
	vasi_suspend_state_t ret;

	/* need reference to current struct kunit *t */
	ctx = container_of(s, struct suspend_test_context, session);

	/* catch bad test setup */
	KUNIT_ASSERT_NOT_ERR_OR_NULL(ctx->test, ctx->state_seq);
	ret = ctx->state_seq[ctx->state_seqno];
	ctx->state_seqno++;

	return ret;
}

vasi_suspend_state_t return_aborted(struct papr_lpar_suspend_session *s)
{
	return VASI_SUSPEND_STATE_ABORTED;
}

static void abort_on_vasi_state_invalid(struct kunit *t)
{
	struct suspend_test_context *ctx = t->priv;
	const struct papr_suspend_ops ops = {
		.poll_vasi_state = test_poll_vasi_state,
	};
	ctx->state_seq = invalid_at_start;

	papr_suspend_session_init(&ctx->session, TEST_VASI_STREAM_ID, &ops);

	KUNIT_EXPECT_EQ(t, -EINVAL, papr_suspend_lpar(&ctx->session));
}

static void vasi_state_aborted(struct kunit *t)
{
	struct suspend_test_context *ctx = t->priv;
	const struct papr_suspend_ops ops = {
		.poll_vasi_state = return_aborted,
	};

	papr_suspend_session_init(&ctx->session, TEST_VASI_STREAM_ID, &ops);

	KUNIT_EXPECT_EQ(t, -ECANCELED, papr_suspend_lpar(&ctx->session));
}

static struct kunit_case lpar_suspend_tests[] = {
	KUNIT_CASE(abort_on_vasi_state_invalid),
	KUNIT_CASE(vasi_state_aborted),
	{},
};

static int lpar_suspend_tsuite_init(struct kunit *t)
{
	struct suspend_test_context *ctx;

	ctx = kunit_kzalloc(t, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(t, ctx);

	t->priv = ctx;
	ctx->test = t;

	return 0;
}

static struct kunit_suite lpar_suspend_tsuite = {
	.name = "pseries partition suspension",
	.init = lpar_suspend_tsuite_init,
	.test_cases = lpar_suspend_tests,
};

kunit_test_suites(&lpar_suspend_tsuite);
MODULE_LICENSE("GPL v2");

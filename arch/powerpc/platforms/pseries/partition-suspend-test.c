// SPDX-License-Identifier: GPL-2.0
/*
 * Tests for code related to the Partition Suspension Option as
 * specified in the Power Architecture® Platform Requirements+.
 */

#include <kunit/test.h>

#include "papr-suspend.h"

#define TEST_VASI_STREAM_ID 0xabcdabcdabcdabcd

/* Need this for use in KUNIT_* equality macros so types match. */
static const vasi_suspend_state_t test_state_seq_end = VASI_SUSPEND_STATE_TEST_SEQ_END;

/* VASI Suspend State Sequence paster macro */
#define V3S(name, first_value, ...)			\
	vasi_suspend_state_t name[] = {			\
		[0] = (first_value),			\
		##__VA_ARGS__,				\
		VASI_SUSPEND_STATE_TEST_SEQ_END,	\
	}

static const V3S(v3s_none, VASI_SUSPEND_STATE_TEST_SEQ_END);
static const V3S(invalid_at_start, VASI_SUSPEND_STATE_INVALID);
static const V3S(enabled_then_aborted,
		 VASI_SUSPEND_STATE_ENABLED,
		 VASI_SUSPEND_STATE_ABORTED);
static const V3S(happy_path,
		 VASI_SUSPEND_STATE_ENABLED,
		 VASI_SUSPEND_STATE_SUSPENDING,
		 VASI_SUSPEND_STATE_RESUMED,
		 VASI_SUSPEND_STATE_COMPLETED);
static const V3S(suspend_fails,
		 VASI_SUSPEND_STATE_ENABLED,
		 VASI_SUSPEND_STATE_SUSPENDING,
		 VASI_SUSPEND_STATE_ABORTED);

typedef struct h_vasi_state_result {
	long hvrc; /* H_Success, H_Parameter, H_Hardware */
	vasi_suspend_state_t r4;
} h_vasi_state_result_t;

#define define_vasi_state_result(rc, ret) \
	(struct h_vasi_state_result) { .hvrc = (rc), .r4 = (ret), }
#define vasi_state_success(ret) \
	define_vasi_state_result(H_SUCCESS, (ret))
#define vasi_state_error(err) \
	define_vasi_state_result((err), VASI_SUSPEND_STATE_SENTINEL)

struct suspend_test_context {
	struct papr_lpar_suspend_session session;
	const vasi_suspend_state_t *state_seq;
	unsigned short state_seqno;
	bool suspend_called;
	bool canceled;
	struct kunit *test;
	struct papr_suspend_ops ops;
};

/* papr_lpar_suspend_session->ops->poll_vasi_state() test doubles */

static vasi_suspend_state_t test_poll_vasi_state(struct papr_lpar_suspend_session *s)
{
	struct suspend_test_context *ctx;
	vasi_suspend_state_t ret;

	ctx = container_of(s, struct suspend_test_context, session);

	/* catch bad test setup */
	KUNIT_ASSERT_NOT_ERR_OR_NULL(ctx->test, ctx->state_seq);
	ret = ctx->state_seq[ctx->state_seqno];

	/* catch state sequence overrun */
	KUNIT_ASSERT_NE(ctx->test, ret, test_state_seq_end);
	ctx->state_seqno++;

	return ret;
}

static vasi_suspend_state_t poll_vasi_state_shouldnt_call(struct papr_lpar_suspend_session *s)
{
	struct suspend_test_context *ctx;

	ctx = container_of(s, struct suspend_test_context, session);
	KUNIT_FAIL(ctx->test, "used poll_vasi_state() callback in error");

	return VASI_SUSPEND_STATE_INVALID;
}

/* papr_lpar_suspend_session->ops->do_suspend() test doubles */

/*
 * TODO: should_call+KUNIT_FAIL() here not necessary if testcases are
 * checking ctx->suspend_called?
*/
#define define_do_suspend_fn(fn_name, rc, should_call)			\
	static int fn_name(struct papr_lpar_suspend_session *s)		\
	{								\
		struct suspend_test_context *ctx;			\
									\
		ctx = container_of(s, struct suspend_test_context,	\
				   session);				\
									\
		ctx->suspend_called = true;				\
									\
		if (!should_call) {					\
			KUNIT_FAIL(ctx->test,				\
				   "used do_suspend() callback in error"); \
		}							\
		return (rc);						\
	}

define_do_suspend_fn(do_suspend_success, 0, true);
define_do_suspend_fn(do_suspend_enomem, -ENOMEM, true);
define_do_suspend_fn(do_suspend_shouldnt_call, 0, true);

/* papr_lpar_suspend_session->ops->cancel_suspend() test doubles */

#define define_cancel_suspend_fn(fn_name, should_call)			\
	static void fn_name(struct papr_lpar_suspend_session *s)	\
	{								\
		struct suspend_test_context *ctx;			\
									\
		ctx = container_of(s, struct suspend_test_context,	\
				   session);				\
		ctx->canceled = true;					\
									\
		if (!should_call) {					\
			KUNIT_FAIL(ctx->test,				\
				   "used cancel_suspend() callback in error"); \
		}							\
	}

define_cancel_suspend_fn(cancel_suspend_success, true);
define_cancel_suspend_fn(cancel_suspend_shouldnt_call, false);

/* Test cases */

/**
 * TC() - Define a suspend testcase.
 *
 * @name: Name of the testcase, to be passed to KUNIT_CASE()
 *
 * @do_suspend_fn: do_suspend() callback to use. Should be NULL if the
 *                 testcase is expected to not invoke a do_suspend() callback.
 *
 * @cancel_suspend_fn: cancel_suspend() callback to use. Should be NULL if the
 *                     testcase is expected to not invoke a @cancel_suspend
 *                     callback.
 *
 * @expected_result: Expected result of papr_suspend_lpar().
 *
 * @...: Variable-length list of ``&typedef h_vasi_state_result_t`` results.
 */
#define TC(tcname,							\
	   do_suspend_fn,						\
	   cancel_suspend_fn,						\
	   expected_result,						\
	   ...)								\
	static const V3S(vsl_ ## tcname, ##__VA_ARGS__);		\
	static void tcname(struct kunit *t)				\
	{								\
		struct suspend_test_context *ctx = t->priv;		\
									\
		ctx->ops.poll_vasi_state = test_poll_vasi_state;	\
		if (do_suspend_fn != NULL)				\
			ctx->ops.do_suspend = do_suspend_fn;		\
		if (cancel_suspend_fn != NULL)				\
			ctx->ops.cancel_suspend = cancel_suspend_fn;	\
		ctx->state_seq = vsl_ ## tcname;			\
									\
		papr_suspend_session_init(&ctx->session,		\
					  TEST_VASI_STREAM_ID,		\
					  &ctx->ops);			\
									\
		KUNIT_EXPECT_EQ(t, expected_result,			\
				papr_suspend_lpar(&ctx->session));	\
		KUNIT_EXPECT_EQ(t, test_state_seq_end,			\
				ctx->state_seq[ctx->state_seqno]);	\
		if (do_suspend_fn != NULL)				\
			KUNIT_EXPECT_TRUE(t, ctx->suspend_called);	\
		else							\
			KUNIT_EXPECT_FALSE(t, ctx->suspend_called);	\
		if (cancel_suspend_fn != NULL)				\
			KUNIT_EXPECT_TRUE(t, ctx->canceled);		\
		else							\
			KUNIT_EXPECT_FALSE(t, ctx->canceled);		\
	}

TC(handle_invalid,
   NULL,
   NULL,
   -EINVAL,
   VASI_SUSPEND_STATE_INVALID);

TC(handle_abort_after_enabled,
   NULL,
   NULL,
   -ECANCELED,
   VASI_SUSPEND_STATE_ENABLED,
   VASI_SUSPEND_STATE_ABORTED);

TC(success_all_states,
   do_suspend_success,
   NULL,
   0,
   VASI_SUSPEND_STATE_ENABLED,
   VASI_SUSPEND_STATE_SUSPENDING,
   VASI_SUSPEND_STATE_RESUMED,
   VASI_SUSPEND_STATE_COMPLETED);

TC(success_skip_resumed,
   do_suspend_success,
   NULL,
   0,
   VASI_SUSPEND_STATE_ENABLED,
   VASI_SUSPEND_STATE_SUSPENDING,
   VASI_SUSPEND_STATE_COMPLETED);

TC(success_fewest_states,
   do_suspend_success,
   NULL,
   0,
   VASI_SUSPEND_STATE_SUSPENDING,
   VASI_SUSPEND_STATE_COMPLETED);

TC(handle_immediate_abort,
   NULL,
   NULL,
   -ECANCELED,
   VASI_SUSPEND_STATE_ABORTED);

TC(handle_enomem_from_suspend,
   do_suspend_enomem,
   cancel_suspend_success,
   -ENOMEM,
   VASI_SUSPEND_STATE_ENABLED,
   VASI_SUSPEND_STATE_SUSPENDING,
   VASI_SUSPEND_STATE_ABORTED);

static struct kunit_case lpar_suspend_tests[] = {
	KUNIT_CASE(handle_invalid),
	KUNIT_CASE(handle_abort_after_enabled),
	KUNIT_CASE(success_all_states),
	KUNIT_CASE(success_skip_resumed),
	KUNIT_CASE(success_fewest_states),
	KUNIT_CASE(handle_immediate_abort),
	KUNIT_CASE(handle_enomem_from_suspend),
	/* TODO: test H_VASI_STATE -> H_Parameter */
	/* TODO: test H_VASI_SIGNAL -> H_Parameter (cancel) */
	/* TODO: test cancelling -> all vasi states */
	{},
};

static int lpar_suspend_tsuite_init(struct kunit *t)
{
	struct suspend_test_context *ctx;

	ctx = kunit_kzalloc(t, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(t, ctx);

	ctx->ops = (struct papr_suspend_ops) {
		.poll_vasi_state = poll_vasi_state_shouldnt_call,
		.do_suspend      = do_suspend_shouldnt_call,
		.cancel_suspend  = cancel_suspend_shouldnt_call,
	};

	ctx->test = t;
	ctx->state_seq = v3s_none;

	t->priv = ctx;

	return 0;
}

static struct kunit_suite lpar_suspend_tsuite = {
	.name = "pseries partition suspension",
	.init = lpar_suspend_tsuite_init,
	.test_cases = lpar_suspend_tests,
};

kunit_test_suites(&lpar_suspend_tsuite);
MODULE_LICENSE("GPL v2");

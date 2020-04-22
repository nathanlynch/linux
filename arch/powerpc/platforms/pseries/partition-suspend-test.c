// SPDX-License-Identifier: GPL-2.0
/*
 * Tests for code related to the Partition Suspension Option as
 * specified in the Power Architecture® Platform Requirements+.
 */

#include <kunit/test.h>

#include "vasi_suspend_session.h"

#define TEST_VASI_STREAM_ID 0xabcdabcdabcdabcd

/* Need this for use in KUNIT_* equality macros so types match. */
static const vasi_suspend_state_t test_state_seq_end = VASI_SUSPEND_STATE_TEST_SEQ_END;

/* VASI Suspend State Sequence paster macro */
#define V3S(name, first_value, ...)					\
	h_vasi_state_result_t name[] = {				\
		(first_value),						\
		##__VA_ARGS__,						\
		h_vasi_state__h_success(VASI_SUSPEND_STATE_TEST_SEQ_END), \
	}

typedef struct h_vasi_state_result {
	long hvrc; /* H_Success, H_Parameter, H_Hardware */
	vasi_suspend_state_t r4;
} h_vasi_state_result_t;

#define define_vasi_state_result(__rc, __state) \
	(h_vasi_state_result_t){ .hvrc = (__rc), .r4 = (__state), }
#define h_vasi_state__h_success(__state) \
	define_vasi_state_result(H_SUCCESS, (__state))
#define h_vasi_state__err(hvrc) \
	define_vasi_state_result((hvrc), VASI_SUSPEND_STATE_SENTINEL)

struct suspend_test_context {
	struct papr_lpar_suspend_session session;
	const h_vasi_state_result_t *state_seq;
	unsigned short state_seqno;
	bool suspend_called;
	bool canceled;
	struct kunit *test;
	struct papr_suspend_ops ops;
};

/* papr_lpar_suspend_session->ops->poll_vasi_state() test doubles */

static int test_poll_vasi_state(struct papr_lpar_suspend_session *s,
				vasi_suspend_state_t *state)
{
	struct suspend_test_context *ctx;
	h_vasi_state_result_t res;
	int ret;

	ctx = container_of(s, struct suspend_test_context, session);

	/* catch bad test setup */
	KUNIT_ASSERT_NOT_ERR_OR_NULL(ctx->test, ctx->state_seq);
	res = ctx->state_seq[ctx->state_seqno];

	/* catch state sequence overrun */
	KUNIT_ASSERT_NE(ctx->test, res.r4, test_state_seq_end);
	ctx->state_seqno++;

	*state = res.r4;

	switch (res.hvrc) {
	case H_PARAMETER:
		ret = -EINVAL;
		break;
	case H_HARDWARE:
		ret = -EIO;
		break;
	default:
		ret = 0;
		break;
	}
	return ret;
}

static int poll_vasi_state_shouldnt_call(struct papr_lpar_suspend_session *s,
					 vasi_suspend_state_t *state)
{
	struct suspend_test_context *ctx;

	ctx = container_of(s, struct suspend_test_context, session);
	KUNIT_FAIL(ctx->test, "used poll_vasi_state() callback in error");

	return -EIO;
}

/* papr_lpar_suspend_session->ops->do_suspend() test doubles */

#define define_do_suspend_fn(fn_name, rc)				\
	static int fn_name(struct papr_lpar_suspend_session *s)		\
	{								\
		struct suspend_test_context *ctx;			\
									\
		ctx = container_of(s, struct suspend_test_context,	\
				   session);				\
									\
		/* Suspend should be attempted only once. */		\
		KUNIT_EXPECT_FALSE(ctx->test, ctx->suspend_called);	\
		ctx->suspend_called = true;				\
									\
		return (rc);						\
	}

define_do_suspend_fn(do_suspend_success, 0);
define_do_suspend_fn(do_suspend_enomem, -ENOMEM);
define_do_suspend_fn(do_suspend_ebusy, -EBUSY);
define_do_suspend_fn(do_suspend_shouldnt_call, 0);

/* papr_lpar_suspend_session->ops->cancel_suspend() test doubles */

#define define_cancel_suspend_fn(fn_name, rc)				\
	static int fn_name(struct papr_lpar_suspend_session *s)		\
	{								\
		struct suspend_test_context *ctx;			\
									\
		ctx = container_of(s, struct suspend_test_context,	\
				   session);				\
									\
		/* Cancellation should be attempted only once. */	\
		KUNIT_EXPECT_FALSE(ctx->test, ctx->canceled);		\
		ctx->canceled = true;					\
									\
		return (rc);						\
	}

define_cancel_suspend_fn(cancel_suspend_success, 0);
define_cancel_suspend_fn(cancel_suspend_einval, -EINVAL);
define_cancel_suspend_fn(cancel_suspend_shouldnt_call, 0);

/* Test cases */

static bool abort_code_valid(u32 code)
{
	/*
	 * The abort/reason code supplied to a H_VASI_SIGNAL
	 * cancellation request from the suspending partition must
	 * have the form 0x06xxxxxx.
	 */
	return code >> 24 == 0x6;
}

static void tc_inner(struct kunit *t,
		     int (*do_suspend_fn)(struct papr_lpar_suspend_session *),
		     int (*cancel_suspend_fn)(struct papr_lpar_suspend_session *),
		     int expected_result,
		     const h_vasi_state_result_t *vasi_states)
{
	struct suspend_test_context *ctx = t->priv;
	u32 abort_code;

	ctx->ops.poll_vasi_state = test_poll_vasi_state;
	if (do_suspend_fn != NULL)
		ctx->ops.do_suspend = do_suspend_fn;
	if (cancel_suspend_fn != NULL)
		ctx->ops.cancel_suspend = cancel_suspend_fn;
	ctx->state_seq = vasi_states;

	papr_suspend_session_init(&ctx->session,
				  TEST_VASI_STREAM_ID,
				  &ctx->ops);

	KUNIT_EXPECT_EQ(t, expected_result,
			papr_suspend_lpar(&ctx->session));
	KUNIT_EXPECT_EQ(t, test_state_seq_end,
			ctx->state_seq[ctx->state_seqno].r4);
	if (do_suspend_fn != NULL)
		KUNIT_EXPECT_TRUE(t, ctx->suspend_called);
	else
		KUNIT_EXPECT_FALSE(t, ctx->suspend_called);
	if (cancel_suspend_fn != NULL)
		KUNIT_EXPECT_TRUE(t, ctx->canceled);
	else
		KUNIT_EXPECT_FALSE(t, ctx->canceled);
	if (expected_result != 0) {
		abort_code = papr_suspend_abort_code(&ctx->session);
		KUNIT_EXPECT_TRUE(t, abort_code_valid(abort_code));
	}
}

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
		tc_inner(t, do_suspend_fn, cancel_suspend_fn,		\
			 expected_result, vsl_ ## tcname);		\
	}

/*
 * Supplied handle is garbage/invalid.
 */
TC(handle_immediate_invalid,
   NULL,
   NULL,
   -EINVAL,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_INVALID));

TC(handle__h_vasi_state__h_parameter,
   NULL,
   NULL,
   -EINVAL,
   h_vasi_state__err(H_PARAMETER));

TC(handle__h_vasi_state__h_hardware,
   NULL,
   NULL,
   -EIO,
   h_vasi_state__err(H_HARDWARE));

/*
 * Supplied handle is invalidated after entering Suspending state,
 * and/or e.g. ibm,suspend-me fails with -900x.
 */
TC(handle_invalid_after_suspending,
   do_suspend_ebusy,
   cancel_suspend_success,
   -EBUSY,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_INVALID));

TC(handle_h_hardware_after_enabled,
   NULL,
   NULL,
   -EIO,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__err(H_HARDWARE));

/*
 * Suspend is administratively aborted relatively early.
 */
TC(handle_abort_after_enabled,
   NULL,
   NULL,
   -ECANCELED,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ABORTED));

/*
 * Suspend is administratively aborted after entering Suspending
 * state, and/or e.g. ibm,suspend-me fails with -900x
 */
TC(handle_abort_after_suspending,
   do_suspend_ebusy,
   cancel_suspend_success,
   -EBUSY,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ABORTED));

/*
 * Each possible suspend state on the "happy path" is encountered
 * once.
 */
TC(success_each_state_once,
   do_suspend_success,
   NULL,
   0,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_COMPLETED));

/*
 * Successful suspend where the VASI session state is Enabled for
 * multiple polls before the platform is ready for the LPAR to
 * suspend.
 */
TC(success_enabled_x10,
   do_suspend_success,
   NULL,
   0,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_COMPLETED));

/*
 * Successful suspend where the VASI session state is Resumed for
 * multiple polls before the platform says the operation is complete.
 */
TC(success_resumed_x10,
   do_suspend_success,
   NULL,
   0,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_COMPLETED));

/*
 * Each possible suspend state (except Resumed) on the "happy path" is
 * encountered once.
 */
TC(success_skip_resumed,
   do_suspend_success,
   NULL,
   0,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_COMPLETED));

/*
 * Each possible suspend state (except Enabled) on the "happy path" is
 * encountered once.
 */
TC(success_skip_enabled,
   do_suspend_success,
   NULL,
   0,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_COMPLETED));

/*
 * The shortest possible suspend operation in terms of distinct VASI
 * session states.
 */
TC(success_fewest_states,
   do_suspend_success,
   NULL,
   0,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_COMPLETED));

/*
 * Suspend is administratively aborted before H_VASI_STATE can return
 * Enabled.
 */
TC(handle_immediate_abort,
   NULL,
   NULL,
   -ECANCELED,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ABORTED));

/*
 * Linux's suspend method encounters -ENOMEM and must ask the platform
 * to cancel the suspend operation.
 */
TC(handle_enomem_from_suspend,
   do_suspend_enomem,
   cancel_suspend_success,
   -ENOMEM,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ABORTED));

/*
 * Linux's suspend method encounters -ENOMEM and must ask the platform
 * to cancel the suspend operation. Concurrently, the suspend
 * operation is administratively aborted, causing H_VASI_SIGNAL to
 * return an error. In this case Linux should not wait for a
 * transition to the Aborted or Invalid VASI session state.
 */
TC(handle_enomem_from_suspend_and_einval_from_cancel,
   do_suspend_enomem,
   cancel_suspend_einval,
   -ENOMEM,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING));

/*
 * Below are weird conditions that probably shouldn't happen unless
 * there are firmware bugs. The important thing is to verify that the
 * suspend code doesn't get stuck waiting for a status transition
 * that's never coming.
 */

/*
 * Invalid VASI session state upon resuming. Nothing for Linux to do
 * but log it and continue.
 */
TC(handle_invalid_after_resume,
   do_suspend_success,
   NULL,
   0,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_INVALID));

TC(handle_resumed_after_enabled,
   NULL,
   NULL,
   -EIO,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED));

TC(generated__aborted,
   NULL,
   NULL,
   -ECANCELED,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ABORTED));

/* Paths beginning with enabled */

TC(generated__enabled_aborted,
   NULL,
   NULL,
   -ECANCELED,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ABORTED));

TC(generated__enabled_invalid,
   NULL,
   NULL,
   -EINVAL,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_INVALID));

TC(generated__enabled_suspending_aborted,
   do_suspend_ebusy,
   cancel_suspend_success,
   -EBUSY,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ABORTED));

TC(generated__enabled_suspending_completed,
   do_suspend_success,
   NULL,
   0,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_COMPLETED));

TC(generated__enabled_suspending_invalid,
   do_suspend_ebusy,
   cancel_suspend_success,
   -EBUSY,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_INVALID));

TC(generated__enabled_suspending_resumed_completed,
   do_suspend_success,
   NULL,
   0,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_COMPLETED));

TC(generated__enabled_suspending_resumed_invalid,
   do_suspend_success,
   NULL,
   0,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ENABLED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_INVALID));

/* Paths beginning with invalid */

TC(generated__invalid,
   NULL,
   NULL,
   -EINVAL,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_INVALID));

/* Paths beginning with suspending */

TC(generated__suspending_aborted,
   do_suspend_ebusy,
   cancel_suspend_success,
   -EBUSY,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_ABORTED));

TC(generated__suspending_completed,
   do_suspend_success,
   NULL,
   0,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_COMPLETED));

TC(generated__suspending_invalid,
   do_suspend_ebusy,
   cancel_suspend_success,
   -EBUSY,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_INVALID));

TC(generated__suspending_resumed_completed,
   do_suspend_success,
   NULL,
   0,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_COMPLETED));

TC(generated__suspending_resumed_invalid,
   do_suspend_success,
   NULL,
   0,
   h_vasi_state__h_success(VASI_SUSPEND_STATE_SUSPENDING),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_RESUMED),
   h_vasi_state__h_success(VASI_SUSPEND_STATE_INVALID));

static struct kunit_case lpar_suspend_tests[] = {
	KUNIT_CASE(handle_immediate_invalid),
	KUNIT_CASE(handle__h_vasi_state__h_parameter),
	KUNIT_CASE(handle__h_vasi_state__h_hardware),
	KUNIT_CASE(handle_invalid_after_suspending),
	KUNIT_CASE(handle_h_hardware_after_enabled),
	KUNIT_CASE(handle_abort_after_enabled),
	KUNIT_CASE(handle_abort_after_suspending),
	KUNIT_CASE(success_each_state_once),
	KUNIT_CASE(success_enabled_x10),
	KUNIT_CASE(success_resumed_x10),
	KUNIT_CASE(success_skip_resumed),
	KUNIT_CASE(success_skip_enabled),
	KUNIT_CASE(success_fewest_states),
	KUNIT_CASE(handle_immediate_abort),
	KUNIT_CASE(handle_enomem_from_suspend),
	KUNIT_CASE(handle_invalid_after_resume),
	KUNIT_CASE(handle_resumed_after_enabled),
	KUNIT_CASE(handle_enomem_from_suspend_and_einval_from_cancel),
	/* TODO: test H_VASI_STATE -> H_Parameter */
	/* TODO: test cancelling -> all vasi states */

	/* Generated tests */
	KUNIT_CASE(generated__aborted),
	KUNIT_CASE(generated__enabled_aborted),
	KUNIT_CASE(generated__enabled_invalid),
	KUNIT_CASE(generated__enabled_suspending_aborted),
	KUNIT_CASE(generated__enabled_suspending_completed),
	KUNIT_CASE(generated__enabled_suspending_invalid),
	KUNIT_CASE(generated__enabled_suspending_resumed_completed),
	KUNIT_CASE(generated__enabled_suspending_resumed_invalid),
	KUNIT_CASE(generated__invalid),
	KUNIT_CASE(generated__suspending_aborted),
	KUNIT_CASE(generated__suspending_completed),
	KUNIT_CASE(generated__suspending_invalid),
	KUNIT_CASE(generated__suspending_resumed_completed),
	KUNIT_CASE(generated__suspending_resumed_invalid),
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

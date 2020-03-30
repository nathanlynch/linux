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

vasi_suspend_state_t return_invalid(struct papr_lpar_suspend_session *s)
{
	return VASI_SUSPEND_STATE_INVALID;
}

vasi_suspend_state_t return_aborted(struct papr_lpar_suspend_session *s)
{
	return VASI_SUSPEND_STATE_ABORTED;
}

static void abort_on_vasi_state_invalid(struct kunit *t)
{
	const struct papr_suspend_ops ops = {
		.poll_vasi_state = return_invalid,
	};
	struct papr_lpar_suspend_session *s;

	s = papr_lpar_suspend_session_new(TEST_VASI_STREAM_ID, &ops);

	KUNIT_EXPECT_EQ(t, -EINVAL, papr_suspend_lpar(s));

	papr_lpar_suspend_session_finalize(s);
}

static void vasi_state_aborted(struct kunit *t)
{
	const struct papr_suspend_ops ops = {
		.poll_vasi_state = return_aborted,
	};
	struct papr_lpar_suspend_session *s;

	s = papr_lpar_suspend_session_new(TEST_VASI_STREAM_ID, &ops);

	KUNIT_EXPECT_EQ(t, -ECANCELED, papr_suspend_lpar(s));

	papr_lpar_suspend_session_finalize(s);
}

static struct kunit_case lpar_suspend_tests[] = {
	KUNIT_CASE(abort_on_vasi_state_invalid),
	KUNIT_CASE(vasi_state_aborted),
	{},
};

static struct kunit_suite lpar_suspend_tsuite = {
	.name = "pseries partition suspension",
	.test_cases = lpar_suspend_tests,
};

kunit_test_suites(&lpar_suspend_tsuite);
MODULE_LICENSE("GPL v2");

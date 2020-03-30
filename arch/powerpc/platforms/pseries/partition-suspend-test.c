// SPDX-License-Identifier: GPL-2.0
/*
 * Tests for code related to the Partition Suspension Option as
 * specified in the Power Architecture® Platform Requirements+.
 */

#include <kunit/test.h>

/* vasi support API */

/* All architected results for VASI state request. */
typedef enum vasi_suspend_state {
	VASI_SUSPEND_STATE_INVALID    = 0,
	VASI_SUSPEND_STATE_ENABLED    = 1,
	VASI_SUSPEND_STATE_ABORTED    = 2,
	VASI_SUSPEND_STATE_SUSPENDING = 3,
	VASI_SUSPEND_STATE_SUSPENDED  = 4,
	VASI_SUSPEND_STATE_RESUMED    = 5,
	VASI_SUSPEND_STATE_COMPLETED  = 6,
	VASI_SUSPEND_STATE_FAILOVER   = 7,
	/*
	 * The following are for convenience and test. They are not
	 * defined in PAPR or used outside of this code.
	 */
	VASI_SUSPEND_STATE_TEST_SEQ_END  = 9998,
	VASI_SUSPEND_STATE_UNINITIALIZED = 9999,
} vasi_suspend_state_t;

struct papr_lpar_suspend_session;

struct papr_suspend_ops {
	vasi_suspend_state_t (*poll_vasi_state)(struct papr_lpar_suspend_session *vsm);
	int (*do_suspend)(struct papr_lpar_suspend_session *vsm);
	void (*cancel_suspend)(u64 handle);
};

struct papr_lpar_suspend_session {
	u64 handle;
	vasi_suspend_state_t state;
	const struct papr_suspend_ops *ops;
};

struct papr_lpar_suspend_session *
papr_lpar_suspend_session_new(u64 handle,
			      const struct papr_suspend_ops *ops)
{
	struct papr_lpar_suspend_session *s;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		goto cancel;

	*s = (struct papr_lpar_suspend_session) {
		.handle = handle,
		.state = VASI_SUSPEND_STATE_UNINITIALIZED,
		.ops = ops,
	};

	return s;
cancel:
	ops->cancel_suspend(handle);
	return s;
}

void papr_lpar_suspend_session_finalize(struct papr_lpar_suspend_session *s)
{
	kfree(s);
}

int papr_suspend_lpar(struct papr_lpar_suspend_session *session)
{
	bool done;
	int ret;

	done = false;
	ret = 0;

	while (!done) {
		session->state = session->ops->poll_vasi_state(session);

		switch (session->state) {
		case VASI_SUSPEND_STATE_INVALID:
			ret = -EINVAL;
			done = true;
			break;
		case VASI_SUSPEND_STATE_ENABLED:
			break;
		case VASI_SUSPEND_STATE_ABORTED:
			ret = -ECANCELED;
			done = true;
			break;
		case VASI_SUSPEND_STATE_SUSPENDING:
			ret = session->ops->do_suspend(session);
			if (ret) {
				session->ops->cancel_suspend(session->handle);
				done = true;
			}
			break;
		case VASI_SUSPEND_STATE_SUSPENDED:
			break;
		case VASI_SUSPEND_STATE_RESUMED:
			break;
		case VASI_SUSPEND_STATE_COMPLETED:
			ret = 0;
			done = true;
			break;
		case VASI_SUSPEND_STATE_FAILOVER:
			break;
		default:
			ret = -EIO;
			done = true;
			break;
		}
	}

	return ret;
}

/****************************************************************/
/******** Test code below                                       */
/****************************************************************/

#define TEST_VASI_STREAM_ID 0xabcdabcdabcdabcd

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

#include <linux/kernel.h>
#include <linux/slab.h>

#include "vasi_suspend_session.h"

void papr_suspend_session_init(struct papr_lpar_suspend_session *s, u64 handle,
			       const struct papr_suspend_ops *ops)
{
	*s = (struct papr_lpar_suspend_session) {
		.handle = handle,
		.state = LPAR_SUSPEND_STARTING,
		.ops = ops,
	};
}

static void step_state(struct papr_lpar_suspend_session *session, vasi_suspend_state_t vasi_state)
{
	switch (session->state) {
	case LPAR_SUSPEND_STARTING:
		switch(vasi_state) {
		case VASI_SUSPEND_STATE_ENABLED:
			/* No change. */
			break;
		case VASI_SUSPEND_STATE_SUSPENDING:
			session->result = session->ops->do_suspend(session);
			session->state = LPAR_SUSPEND_RESUMING;
			if (session->result) {
				int ret;

				pr_err("partition suspend error %i; "
				       "attempting to cancel", session->result);

				session->state = LPAR_SUSPEND_CANCELING;

				ret = session->ops->cancel_suspend(session);
				if (ret) {
					pr_err("Attempt to cancel suspend session "
					       "failed with %i\n", ret);
					session->state = LPAR_SUSPEND_DONE;
				}
			}
			break;
		case VASI_SUSPEND_STATE_ABORTED:
			session->result = -ECANCELED;
			session->state = LPAR_SUSPEND_DONE;
			break;
		case VASI_SUSPEND_STATE_INVALID:
			session->result = -EINVAL;
			session->state = LPAR_SUSPEND_DONE;
			break;
		default:
			pr_err("Unexpected VASI migration session state %i "
			       "while beginning suspend\n", vasi_state);
			session->result = -EIO;
			session->state = LPAR_SUSPEND_DONE;
			break;
		}
		break;
	case LPAR_SUSPEND_RESUMING:
		switch(vasi_state) {
		case VASI_SUSPEND_STATE_RESUMED:
			/* No change. */
			break;
		case VASI_SUSPEND_STATE_COMPLETED:
			session->state = LPAR_SUSPEND_DONE;
			break;
		default:
			pr_err("Unexpected VASI migration session state %i "
			       "while resuming\n", vasi_state);
			session->state = LPAR_SUSPEND_DONE;
			break;
		}
		break;
	case LPAR_SUSPEND_CANCELING:
		/*
		 * If we're waiting for the platform to acknowledge
		 * the cancellation, then session->result already has
		 * the -errno and we don't set it here.
		 */
		switch(vasi_state) {
		case VASI_SUSPEND_STATE_ENABLED:
		case VASI_SUSPEND_STATE_SUSPENDING:
			/* No change. */
			break;
		case VASI_SUSPEND_STATE_INVALID:
		case VASI_SUSPEND_STATE_ABORTED:
			session->state = LPAR_SUSPEND_DONE;
			break;
		default:
			pr_err("Unexpected VASI migration session state %i "
			       "while canceling\n", vasi_state);
			session->state = LPAR_SUSPEND_DONE;
			break;
		}
		break;
	default:
		pr_err("Unexpected LPAR suspend session state %i\n",
		       session->state);
		session->result = -EIO;
		break;
	}
}

int papr_suspend_lpar(struct papr_lpar_suspend_session *session)
{
	while (session->state != LPAR_SUSPEND_DONE) {
		vasi_suspend_state_t vasi_state;
		int err;

		/* TODO: Make interruptible/killable. */
		err = session->ops->poll_vasi_state(session, &vasi_state);
		if (err) {
			pr_err("H_VASI_STATE(0x%llx) unexpectedly returned %i",
			       session->handle, err);
			session->result = err;
			break;
		}

		step_state(session, vasi_state);
	}

	return session->result;
}

u32 papr_suspend_abort_code(const struct papr_lpar_suspend_session *session)
{
	u32 ret;

	if (session->result == 0) {
		/*
		 * It is an error to call this if the suspend
		 * operation has not actually encountered a problem.
		 */
		pr_warn_once("No valid abort code for successful suspend.\n");
		ret = 0;
	} else {
		/*
		 * The first byte of the reason/abort code must be
		 * 0x06 to indicate the failing entity is the
		 * suspending partition. The remaining three bytes are
		 * opaque to the platform; we simply record the
		 * positive errno value.
		 */
		ret = (0x6 << 24) | (abs(session->result) & 0xffffff);
	}

	return ret;
}

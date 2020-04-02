#include <linux/kernel.h>
#include <linux/slab.h>

#include "papr-suspend.h"

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
				session->ops->cancel_suspend(session);
				pr_err("partition suspend error %i; "
				       "attempting to cancel", session->result);

				/*
				 * TODO: Should bail on H_Parameter et
				 * al from H_VASI_SIGNAL. Doesn't make
				 * sense to continue polling the VASI
				 * state on error.
				 */
				session->state = LPAR_SUSPEND_CANCELING;
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

		/* TODO: Handle H_Parameter et al from VASI state call. */
		/* TODO: Make interruptible/killable. */
		vasi_state = session->ops->poll_vasi_state(session);

		step_state(session, vasi_state);
	}

	return session->result;
}

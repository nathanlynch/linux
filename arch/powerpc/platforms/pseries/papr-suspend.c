#include <linux/kernel.h>
#include <linux/slab.h>

#include "papr-suspend.h"

void papr_suspend_session_init(struct papr_lpar_suspend_session *s, u64 handle,
			       const struct papr_suspend_ops *ops)
{
	*s = (struct papr_lpar_suspend_session) {
		.handle = handle,
		.state = VASI_SUSPEND_STATE_UNINITIALIZED,
		.ops = ops,
	};
}

struct papr_lpar_suspend_session *
papr_lpar_suspend_session_new(u64 handle,
			      const struct papr_suspend_ops *ops)
{
	struct papr_lpar_suspend_session *s;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		goto cancel;

	papr_suspend_session_init(s, handle, ops);

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

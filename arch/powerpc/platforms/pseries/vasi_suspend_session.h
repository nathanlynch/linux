#ifndef VASI_SUSPEND_SESSION_H
#define VASI_SUSPEND_SESSION_H

#include <linux/types.h>

/* All architected results for VASI state request. */
typedef enum vasi_suspend_state {
	/*
	 * States which are observable from the suspending partition
	 * using the H_VASI_STATE hcall.
	 */
	VASI_SUSPEND_STATE_INVALID    = 0, /* Invalid handle */
	VASI_SUSPEND_STATE_ENABLED    = 1, /* Transfer in progress */
	VASI_SUSPEND_STATE_ABORTED    = 2, /* Session aborted */
	VASI_SUSPEND_STATE_SUSPENDING = 3, /* Ready for join+suspend-me */
	VASI_SUSPEND_STATE_RESUMED    = 5, /* Now executing on destination */
	VASI_SUSPEND_STATE_COMPLETED  = 6, /* Operation complete */
	/*
	 * States which are architected but should not be seen by the
	 * suspending partition.
	 */
	VASI_SUSPEND_STATE_SUSPENDED  = 4,
	VASI_SUSPEND_STATE_FAILOVER   = 7,
	/*
	 * The following are for convenience and test. They are not
	 * defined in PAPR or used outside of this code.
	 */
	VASI_SUSPEND_STATE_TEST_SEQ_END  = 9998,
	VASI_SUSPEND_STATE_SENTINEL      = 9999,
} vasi_suspend_state_t;

/* Logical suspend operation states, distinct from VASI states. */
typedef enum lpar_suspend_states {
	LPAR_SUSPEND_STARTING,
	LPAR_SUSPEND_RESUMING,
	LPAR_SUSPEND_CANCELING,
	LPAR_SUSPEND_DONE,
} lp_suspend_state_t;

struct vasi_suspend_ops;

struct vasi_suspend_session {
/* private: All fields for internal use. */
	u64 handle;
	lp_suspend_state_t state;
	int result;
	const struct vasi_suspend_ops *ops;
};

struct vasi_suspend_ops {
	int (*poll_vasi_state)(struct vasi_suspend_session *session,
			       vasi_suspend_state_t *state);
	int (*do_suspend)(struct vasi_suspend_session *s);
	int (*cancel_suspend)(struct vasi_suspend_session *s);
};

void vasi_suspend_session_init(struct vasi_suspend_session *s, u64 handle,
			       const struct vasi_suspend_ops *ops);

int vasi_suspend_session_run(struct vasi_suspend_session *session);
u32 vasi_suspend_session_abort_code(const struct vasi_suspend_session *session);

#endif /* VASI_SUSPEND_SESSION_H */

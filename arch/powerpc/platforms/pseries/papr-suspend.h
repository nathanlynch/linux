#ifndef PAPR_SUSPEND_H
#define PAPR_SUSPEND_H

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

struct papr_suspend_ops;

struct papr_lpar_suspend_session {
	u64 handle;
	lp_suspend_state_t state;
	int result;
	const struct papr_suspend_ops *ops;
};

struct papr_suspend_ops {
	int (*poll_vasi_state)(struct papr_lpar_suspend_session *session,
			       vasi_suspend_state_t *state);
	int (*do_suspend)(struct papr_lpar_suspend_session *s);
	int (*cancel_suspend)(struct papr_lpar_suspend_session *s);
};

void papr_suspend_session_init(struct papr_lpar_suspend_session *s, u64 handle,
			       const struct papr_suspend_ops *ops);

int papr_suspend_lpar(struct papr_lpar_suspend_session *session);

#endif /* PAPR_SUSPEND_H */

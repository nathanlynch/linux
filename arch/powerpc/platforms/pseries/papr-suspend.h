#ifndef PAPR_SUSPEND_H
#define PAPR_SUSPEND_H

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

struct papr_suspend_ops;

struct papr_lpar_suspend_session {
	u64 handle;
	vasi_suspend_state_t state;
	const struct papr_suspend_ops *ops;
};

struct papr_suspend_ops {
	vasi_suspend_state_t (*poll_vasi_state)(struct papr_lpar_suspend_session *s);
	int (*do_suspend)(struct papr_lpar_suspend_session *s);
	void (*cancel_suspend)(u64 handle);
};

void papr_suspend_session_init(struct papr_lpar_suspend_session *s, u64 handle,
			       const struct papr_suspend_ops *ops);
int papr_suspend_lpar(struct papr_lpar_suspend_session *session);

#endif /* PAPR_SUSPEND_H */

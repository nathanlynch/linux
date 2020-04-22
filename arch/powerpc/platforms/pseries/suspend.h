#ifndef PSERIES_SUSPEND_H
#define PSERIES_SUSPEND_H

struct papr_suspend_ops;
const struct papr_suspend_ops *pseries_suspend_default_ops(void);

#endif

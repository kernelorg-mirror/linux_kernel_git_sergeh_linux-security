/*
 *  Copyright (C) 2014 Google Inc.
 *
 *  Author: Aditya Kali (adityakali@google.com)
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation, version 2 of the License.
 */

#include <linux/cgroup.h>
#include <linux/cgroup_namespace.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/nsproxy.h>
#include <linux/proc_ns.h>

const struct proc_ns_operations cgroupns_operations;

static struct cgroup_namespace *alloc_cgroup_ns(void)
{
	struct cgroup_namespace *new_ns;
	int ret;

	new_ns = kzalloc(sizeof(struct cgroup_namespace), GFP_KERNEL);
	if (!new_ns)
		return ERR_PTR(-ENOMEM);
	ret = ns_alloc_inum(&new_ns->ns);
	if (ret) {
		kfree(new_ns);
		return ERR_PTR(ret);
	}
	atomic_set(&new_ns->count, 1);
	new_ns->ns.ops = &cgroupns_operations;
	return new_ns;
}

extern void put_css_set(struct css_set *cset);
extern  void get_css_set(struct css_set *cset);

void free_cgroup_ns(struct cgroup_namespace *ns)
{
	put_css_set(ns->root_cgrps);
	put_user_ns(ns->user_ns);
	ns_free_inum(&ns->ns);
	kfree(ns);
}
EXPORT_SYMBOL(free_cgroup_ns);

struct cgroup_namespace *copy_cgroup_ns(unsigned long flags,
					struct user_namespace *user_ns,
					struct cgroup_namespace *old_ns)
{
	struct cgroup_namespace *new_ns = NULL;
	struct css_set *cgrps = NULL;
	int err;

	BUG_ON(!old_ns);

	if (!(flags & CLONE_NEWCGROUP))
		return get_cgroup_ns(old_ns);

	/* Allow only sysadmin to create cgroup namespace. */
	err = -EPERM;
	if (!ns_capable(user_ns, CAP_SYS_ADMIN))
		goto err_out;

	cgrps = task_css_set(current);
	get_css_set(cgrps);

	err = -ENOMEM;
	new_ns = alloc_cgroup_ns();
	if (!new_ns)
		goto err_out;

	new_ns->user_ns = get_user_ns(user_ns);
	new_ns->root_cgrps = cgrps;

	return new_ns;

err_out:
	if (cgrps)
		put_css_set(cgrps);
	kfree(new_ns);
	return ERR_PTR(err);
}

static inline struct cgroup_namespace *to_cg_ns(struct ns_common *ns) {
	return container_of(ns, struct cgroup_namespace, ns);
}

static int cgroupns_install(struct nsproxy *nsproxy, struct ns_common *ns)
{
	struct cgroup_namespace *cgroup_ns = to_cg_ns(ns);

	if (!ns_capable(current_user_ns(), CAP_SYS_ADMIN) ||
	    !ns_capable(cgroup_ns->user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	/* Don't need to do anything if we are attaching to our own cgroupns. */
	if (cgroup_ns == nsproxy->cgroup_ns)
		return 0;

	get_cgroup_ns(cgroup_ns);
	put_cgroup_ns(nsproxy->cgroup_ns);
	nsproxy->cgroup_ns = cgroup_ns;

	return 0;
}

static struct ns_common *cgroupns_get(struct task_struct *task)
{
	struct cgroup_namespace *ns = NULL;
	struct nsproxy *nsproxy;

	task_lock(task);
	nsproxy = task->nsproxy;
	if (nsproxy) {
		ns = nsproxy->cgroup_ns;
		get_cgroup_ns(ns);
	}
	task_unlock(task);

	return ns ? &ns->ns : NULL;
}

static void cgroupns_put(struct ns_common *ns)
{
	put_cgroup_ns(to_cg_ns(ns));
}

const struct proc_ns_operations cgroupns_operations = {
	.name		= "cgroup",
	.type		= CLONE_NEWCGROUP,
	.get		= cgroupns_get,
	.put		= cgroupns_put,
	.install	= cgroupns_install,
};

static __init int cgroup_namespaces_init(void)
{
	return 0;
}
subsys_initcall(cgroup_namespaces_init);

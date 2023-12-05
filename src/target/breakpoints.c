// SPDX-License-Identifier: GPL-2.0-or-later

/***************************************************************************
 *   Copyright (C) 2005 by Dominic Rath                                    *
 *   Dominic.Rath@gmx.de                                                   *
 *                                                                         *
 *   Copyright (C) ST-Ericsson SA 2011                                     *
 *   michel.jaouen@stericsson.com : smp minimum support                    *
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "target.h"
#include <helper/log.h>
#include "breakpoints.h"
#include "rtos/rtos.h"
#include "smp.h"

enum breakpoint_watchpoint {
	BREAKPOINT,
	WATCHPOINT,
};

static const char * const breakpoint_type_strings[] = {
	"hardware",
	"software"
};

static const char * const watchpoint_rw_strings[] = {
	"read",
	"write",
	"access"
};

/* monotonic counter/id-number for breakpoints and watch points */
static int bpwp_unique_id;

static int breakpoint_add_internal(struct target *target,
	target_addr_t address,
	uint32_t length,
	enum breakpoint_type type)
{
	struct breakpoint *breakpoint = target->breakpoints;
	struct breakpoint **breakpoint_p = &target->breakpoints;
	const char *reason;
	int retval;

	while (breakpoint) {
		if (breakpoint->address == address) {
			/* FIXME don't assume "same address" means "same
			 * breakpoint" ... check all the parameters before
			 * succeeding.
			 */
			LOG_ERROR("Duplicate Breakpoint address: " TARGET_ADDR_FMT " (BP %" PRIu32 ")",
				address, breakpoint->unique_id);
			return ERROR_TARGET_DUPLICATE_BREAKPOINT;
		}
		breakpoint_p = &breakpoint->next;
		breakpoint = breakpoint->next;
	}

	(*breakpoint_p) = malloc(sizeof(struct breakpoint));
	(*breakpoint_p)->address = address;
	(*breakpoint_p)->asid = 0;
	(*breakpoint_p)->length = length;
	(*breakpoint_p)->type = type;
	(*breakpoint_p)->is_set = false;
	(*breakpoint_p)->orig_instr = malloc(length);
	(*breakpoint_p)->next = NULL;
	(*breakpoint_p)->unique_id = bpwp_unique_id++;

	retval = target_add_breakpoint(target, *breakpoint_p);
	switch (retval) {
		case ERROR_OK:
			break;
		case ERROR_TARGET_RESOURCE_NOT_AVAILABLE:
			reason = "resource not available";
			goto fail;
		case ERROR_TARGET_NOT_HALTED:
			reason = "target not halted";
			goto fail;
		default:
			reason = "unknown reason";
fail:
			LOG_ERROR("can't add breakpoint: %s", reason);
			free((*breakpoint_p)->orig_instr);
			free(*breakpoint_p);
			*breakpoint_p = NULL;
			return retval;
	}

	LOG_DEBUG("[%d] added %s breakpoint at " TARGET_ADDR_FMT
			" of length 0x%8.8x, (BPID: %" PRIu32 ")",
		target->coreid,
		breakpoint_type_strings[(*breakpoint_p)->type],
		(*breakpoint_p)->address, (*breakpoint_p)->length,
		(*breakpoint_p)->unique_id);

	return ERROR_OK;
}

static int context_breakpoint_add_internal(struct target *target,
	uint32_t asid,
	uint32_t length,
	enum breakpoint_type type)
{
	struct breakpoint *breakpoint = target->breakpoints;
	struct breakpoint **breakpoint_p = &target->breakpoints;
	int retval;

	while (breakpoint) {
		if (breakpoint->asid == asid) {
			/* FIXME don't assume "same address" means "same
			 * breakpoint" ... check all the parameters before
			 * succeeding.
			 */
			LOG_ERROR("Duplicate Breakpoint asid: 0x%08" PRIx32 " (BP %" PRIu32 ")",
				asid, breakpoint->unique_id);
			return ERROR_TARGET_DUPLICATE_BREAKPOINT;
		}
		breakpoint_p = &breakpoint->next;
		breakpoint = breakpoint->next;
	}

	(*breakpoint_p) = malloc(sizeof(struct breakpoint));
	(*breakpoint_p)->address = 0;
	(*breakpoint_p)->asid = asid;
	(*breakpoint_p)->length = length;
	(*breakpoint_p)->type = type;
	(*breakpoint_p)->is_set = false;
	(*breakpoint_p)->orig_instr = malloc(length);
	(*breakpoint_p)->next = NULL;
	(*breakpoint_p)->unique_id = bpwp_unique_id++;
	retval = target_add_context_breakpoint(target, *breakpoint_p);
	if (retval != ERROR_OK) {
		LOG_ERROR("could not add breakpoint");
		free((*breakpoint_p)->orig_instr);
		free(*breakpoint_p);
		*breakpoint_p = NULL;
		return retval;
	}

	LOG_DEBUG("added %s Context breakpoint at 0x%8.8" PRIx32 " of length 0x%8.8x, (BPID: %" PRIu32 ")",
		breakpoint_type_strings[(*breakpoint_p)->type],
		(*breakpoint_p)->asid, (*breakpoint_p)->length,
		(*breakpoint_p)->unique_id);

	return ERROR_OK;
}

static int hybrid_breakpoint_add_internal(struct target *target,
	target_addr_t address,
	uint32_t asid,
	uint32_t length,
	enum breakpoint_type type)
{
	struct breakpoint *breakpoint = target->breakpoints;
	struct breakpoint **breakpoint_p = &target->breakpoints;
	int retval;

	while (breakpoint) {
		if ((breakpoint->asid == asid) && (breakpoint->address == address)) {
			/* FIXME don't assume "same address" means "same
			 * breakpoint" ... check all the parameters before
			 * succeeding.
			 */
			LOG_ERROR("Duplicate Hybrid Breakpoint asid: 0x%08" PRIx32 " (BP %" PRIu32 ")",
				asid, breakpoint->unique_id);
			return ERROR_TARGET_DUPLICATE_BREAKPOINT;
		} else if ((breakpoint->address == address) && (breakpoint->asid == 0)) {
			LOG_ERROR("Duplicate Breakpoint IVA: " TARGET_ADDR_FMT " (BP %" PRIu32 ")",
				address, breakpoint->unique_id);
			return ERROR_TARGET_DUPLICATE_BREAKPOINT;

		}
		breakpoint_p = &breakpoint->next;
		breakpoint = breakpoint->next;
	}
	(*breakpoint_p) = malloc(sizeof(struct breakpoint));
	(*breakpoint_p)->address = address;
	(*breakpoint_p)->asid = asid;
	(*breakpoint_p)->length = length;
	(*breakpoint_p)->type = type;
	(*breakpoint_p)->is_set = false;
	(*breakpoint_p)->orig_instr = malloc(length);
	(*breakpoint_p)->next = NULL;
	(*breakpoint_p)->unique_id = bpwp_unique_id++;


	retval = target_add_hybrid_breakpoint(target, *breakpoint_p);
	if (retval != ERROR_OK) {
		LOG_ERROR("could not add breakpoint");
		free((*breakpoint_p)->orig_instr);
		free(*breakpoint_p);
		*breakpoint_p = NULL;
		return retval;
	}
	LOG_DEBUG(
		"added %s Hybrid breakpoint at address " TARGET_ADDR_FMT " of length 0x%8.8x, (BPID: %" PRIu32 ")",
		breakpoint_type_strings[(*breakpoint_p)->type],
		(*breakpoint_p)->address,
		(*breakpoint_p)->length,
		(*breakpoint_p)->unique_id);

	return ERROR_OK;
}

int breakpoint_add(struct target *target,
	target_addr_t address,
	uint32_t length,
	enum breakpoint_type type)
{
	if (target->smp && type == BKPT_HARD) {
		struct target_list *list_node;
		foreach_smp_target(list_node, target->smp_targets) {
			struct target *curr = list_node->target;
			if (curr->state == TARGET_UNAVAILABLE)
				continue;
			int retval = breakpoint_add_internal(curr, address, length, type);
			if (retval != ERROR_OK)
				return retval;
		}

		return ERROR_OK;
	} else {
		/* For software breakpoints on SMP targets, only set them on a
		 * single target. We assume that SMP targets share memory. */
		return breakpoint_add_internal(target, address, length, type);
	}
}

int context_breakpoint_add(struct target *target,
	uint32_t asid,
	uint32_t length,
	enum breakpoint_type type)
{
	if (target->smp) {
		struct target_list *head;

		foreach_smp_target(head, target->smp_targets) {
			struct target *curr = head->target;
			if (curr->state == TARGET_UNAVAILABLE)
				continue;
			int retval = context_breakpoint_add_internal(curr, asid, length, type);
			if (retval != ERROR_OK)
				return retval;
		}

		return ERROR_OK;
	} else {
		return context_breakpoint_add_internal(target, asid, length, type);
	}
}

int hybrid_breakpoint_add(struct target *target,
	target_addr_t address,
	uint32_t asid,
	uint32_t length,
	enum breakpoint_type type)
{
	if (target->smp) {
		struct target_list *head;

		foreach_smp_target(head, target->smp_targets) {
			struct target *curr = head->target;
			if (curr->state == TARGET_UNAVAILABLE)
				continue;
			int retval = hybrid_breakpoint_add_internal(curr, address, asid, length, type);
			if (retval != ERROR_OK)
				return retval;
		}

		return ERROR_OK;
	} else
		return hybrid_breakpoint_add_internal(target, address, asid, length, type);
}

/* Free the data structures we use to track a breakpoint on data_target.
 * Remove the actual breakpoint from breakpoint_target.
 * This separation is useful when a software breakpoint is tracked on a target
 * that is currently unavailable, but the breakpoint also affects a target that
 * is available.
 */
static int breakpoint_free(struct target *data_target, struct target *breakpoint_target,
		struct breakpoint *breakpoint_to_remove)
{
	struct breakpoint *breakpoint = data_target->breakpoints;
	struct breakpoint **breakpoint_p = &data_target->breakpoints;
	int retval;

	while (breakpoint) {
		if (breakpoint == breakpoint_to_remove)
			break;
		breakpoint_p = &breakpoint->next;
		breakpoint = breakpoint->next;
	}

	if (!breakpoint)
		return ERROR_BREAKPOINT_NOT_FOUND;

	retval = target_remove_breakpoint(breakpoint_target, breakpoint);
	if (retval != ERROR_OK) {
		LOG_TARGET_ERROR(breakpoint_target, "could not remove breakpoint #%d on this target",
						breakpoint->number);
		return retval;
	}

	LOG_DEBUG("free BPID: %" PRIu32 " --> %d", breakpoint->unique_id, retval);
	(*breakpoint_p) = breakpoint->next;
	free(breakpoint->orig_instr);
	free(breakpoint);

	return ERROR_OK;
}

static int breakpoint_remove_all_internal(struct target *target)
{
	LOG_TARGET_DEBUG(target, "Delete all breakpoints");

	struct breakpoint *breakpoint = target->breakpoints;
	int retval = ERROR_OK;

	while (breakpoint) {
		struct breakpoint *tmp = breakpoint;
		breakpoint = breakpoint->next;
		int status = breakpoint_free(target, target, tmp);
		if (status != ERROR_OK)
			retval = status;
	}

	return retval;
}

int breakpoint_remove(struct target *target, target_addr_t address)
{
	if (!target->smp) {
		struct breakpoint *breakpoint = breakpoint_find(target, address);
		if (breakpoint)
			return breakpoint_free(target, target, breakpoint);
		return ERROR_BREAKPOINT_NOT_FOUND;
	}

	int retval = ERROR_OK;
	unsigned int found = 0;
	struct target_list *head;
	/* Target where we found a software breakpoint. */
	struct target *software_breakpoint_target = NULL;
	struct breakpoint *software_breakpoint = NULL;
	/* Target that is available. */
	struct target *available_target = NULL;
	/* Target that is available and halted. */
	struct target *halted_target = NULL;

	foreach_smp_target(head, target->smp_targets) {
		struct target *curr = head->target;

		if (!available_target && curr->state != TARGET_UNAVAILABLE)
			available_target = curr;
		if (!halted_target && curr->state == TARGET_HALTED)
			halted_target = curr;

		struct breakpoint *breakpoint = breakpoint_find(curr, address);
		if (!breakpoint)
			continue;

		found++;

		if (breakpoint->type == BKPT_SOFT) {
			/* Software breakpoints are set on only one of the SMP
			 * targets.  We can remove them through any of the SMP
			 * targets. */
			if (software_breakpoint_target) {
				LOG_TARGET_WARNING(curr, "Already found software breakpoint at "
						TARGET_ADDR_FMT " on %s.", address, target_name(software_breakpoint_target));
			} else {
				assert(!software_breakpoint_target);
				software_breakpoint_target = curr;
				software_breakpoint = breakpoint;
			}
		} else {
			int status = breakpoint_free(curr, curr, breakpoint);
			if (status != ERROR_OK)
				retval = status;
		}
	}

	if (!found) {
		LOG_ERROR("no breakpoint at address " TARGET_ADDR_FMT " found", address);
		return ERROR_BREAKPOINT_NOT_FOUND;
	}

	if (software_breakpoint) {
		struct target *remove_target;
		if (software_breakpoint_target->state == TARGET_HALTED)
			remove_target = software_breakpoint_target;
		else if (halted_target)
			remove_target = halted_target;
		else
			remove_target = available_target;

		if (remove_target) {
			LOG_DEBUG("Removing software breakpoint found on %s using %s (address="
					TARGET_ADDR_FMT ").",
					target_name(software_breakpoint_target),
					target_name(remove_target),
					address);
			/* Remove the software breakpoint through
			* remove_target, but update the breakpoints structure
			* of software_breakpoint_target. */
			int status = breakpoint_free(software_breakpoint_target, remove_target, software_breakpoint);
			if (status != ERROR_OK)
				/* TODO: If there is an error, can we try to remove the
				* same breakpoint from a different target? */
				retval = status;
		} else {
			LOG_WARNING("No halted target found to remove software breakpoint at "
					TARGET_ADDR_FMT ".", address);
		}
	}

	return retval;
}

static int watchpoint_free(struct target *target, struct watchpoint *watchpoint_to_remove)
{
	struct watchpoint *watchpoint = target->watchpoints;
	struct watchpoint **watchpoint_p = &target->watchpoints;
	int retval;

	while (watchpoint) {
		if (watchpoint == watchpoint_to_remove)
			break;
		watchpoint_p = &watchpoint->next;
		watchpoint = watchpoint->next;
	}

	if (!watchpoint)
		return ERROR_WATCHPOINT_NOT_FOUND;
	retval = target_remove_watchpoint(target, watchpoint);
	if (retval != ERROR_OK) {
		LOG_TARGET_ERROR(target, "could not remove watchpoint #%d on this target",
						 watchpoint->number);
		return retval;
	}

	LOG_DEBUG("free WPID: %d --> %d", watchpoint->unique_id, retval);
	(*watchpoint_p) = watchpoint->next;
	free(watchpoint);

	return ERROR_OK;
}

static int watchpoint_remove_all_internal(struct target *target)
{
	struct watchpoint *watchpoint = target->watchpoints;
	int retval = ERROR_OK;

	while (watchpoint) {
		struct watchpoint *tmp = watchpoint;
		watchpoint = watchpoint->next;
		int status = watchpoint_free(target, tmp);
		if (status != ERROR_OK)
			retval = status;
	}

	return retval;
}

int breakpoint_watchpoint_remove_all(struct target *target, enum breakpoint_watchpoint bp_wp)
{
	assert(bp_wp == BREAKPOINT || bp_wp == WATCHPOINT);
	int retval = ERROR_OK;
	if (target->smp) {
		struct target_list *head;

		foreach_smp_target(head, target->smp_targets) {
			struct target *curr = head->target;

			int status = ERROR_OK;
			if (bp_wp == BREAKPOINT)
				status = breakpoint_remove_all_internal(curr);
			else
				status = watchpoint_remove_all_internal(curr);

			if (status != ERROR_OK)
				retval = status;
		}
	} else {
		if (bp_wp == BREAKPOINT)
			retval = breakpoint_remove_all_internal(target);
		else
			retval = watchpoint_remove_all_internal(target);
	}

	return retval;
}

int breakpoint_remove_all(struct target *target)
{
	return breakpoint_watchpoint_remove_all(target, BREAKPOINT);
}

int watchpoint_remove_all(struct target *target)
{
	return breakpoint_watchpoint_remove_all(target, WATCHPOINT);
}

int breakpoint_clear_target(struct target *target)
{
	int retval = ERROR_OK;

	if (target->smp) {
		struct target_list *head;

		foreach_smp_target(head, target->smp_targets) {
			struct target *curr = head->target;
			int status = breakpoint_remove_all_internal(curr);

			if (status != ERROR_OK)
				retval = status;
		}
	} else {
		retval = breakpoint_remove_all_internal(target);
	}

	return retval;
}

struct breakpoint *breakpoint_find(struct target *target, target_addr_t address)
{
	struct breakpoint *breakpoint = target->breakpoints;

	while (breakpoint) {
		if (breakpoint->address == address ||
				(breakpoint->address == 0 && breakpoint->asid == address))
			return breakpoint;
		breakpoint = breakpoint->next;
	}

	return NULL;
}

static int watchpoint_add_internal(struct target *target, target_addr_t address,
		uint32_t length, enum watchpoint_rw rw, uint64_t value, uint64_t mask)
{
	struct watchpoint *watchpoint = target->watchpoints;
	struct watchpoint **watchpoint_p = &target->watchpoints;
	int retval;
	const char *reason;

	while (watchpoint) {
		if (watchpoint->address == address) {
			if (watchpoint->length != length
				|| watchpoint->value != value
				|| watchpoint->mask != mask
				|| watchpoint->rw != rw) {
				LOG_ERROR("address " TARGET_ADDR_FMT
					" already has watchpoint %d",
					address, watchpoint->unique_id);
				return ERROR_FAIL;
			}

			/* ignore duplicate watchpoint */
			return ERROR_OK;
		}
		watchpoint_p = &watchpoint->next;
		watchpoint = watchpoint->next;
	}

	(*watchpoint_p) = calloc(1, sizeof(struct watchpoint));
	(*watchpoint_p)->address = address;
	(*watchpoint_p)->length = length;
	(*watchpoint_p)->value = value;
	(*watchpoint_p)->mask = mask;
	(*watchpoint_p)->rw = rw;
	(*watchpoint_p)->unique_id = bpwp_unique_id++;

	retval = target_add_watchpoint(target, *watchpoint_p);
	switch (retval) {
		case ERROR_OK:
			break;
		case ERROR_TARGET_RESOURCE_NOT_AVAILABLE:
			reason = "resource not available";
			goto bye;
		case ERROR_TARGET_NOT_HALTED:
			reason = "target not halted";
			goto bye;
		default:
			reason = "unrecognized error";
bye:
			LOG_ERROR("can't add %s watchpoint at " TARGET_ADDR_FMT ", %s",
				watchpoint_rw_strings[(*watchpoint_p)->rw],
				address, reason);
			free(*watchpoint_p);
			*watchpoint_p = NULL;
			return retval;
	}

	LOG_DEBUG("[%d] added %s watchpoint at " TARGET_ADDR_FMT
			" of length 0x%8.8" PRIx32 " (WPID: %d)",
		target->coreid,
		watchpoint_rw_strings[(*watchpoint_p)->rw],
		(*watchpoint_p)->address,
		(*watchpoint_p)->length,
		(*watchpoint_p)->unique_id);

	return ERROR_OK;
}

int watchpoint_add(struct target *target, target_addr_t address,
		uint32_t length, enum watchpoint_rw rw, uint64_t value, uint64_t mask)
{
	if (target->smp) {
		struct target_list *head;

		foreach_smp_target(head, target->smp_targets) {
			struct target *curr = head->target;
			if (curr->state == TARGET_UNAVAILABLE)
				continue;
			int retval = watchpoint_add_internal(curr, address, length, rw, value, mask);
			if (retval != ERROR_OK)
				return retval;
		}

		return ERROR_OK;
	} else {
		return watchpoint_add_internal(target, address, length, rw, value,
				mask);
	}
}

static int watchpoint_remove_internal(struct target *target, target_addr_t address)
{
	struct watchpoint *watchpoint = target->watchpoints;

	while (watchpoint) {
		if (watchpoint->address == address)
			break;
		watchpoint = watchpoint->next;
	}

	if (watchpoint) {
		return watchpoint_free(target, watchpoint);
	} else {
		return ERROR_WATCHPOINT_NOT_FOUND;
	}
}

int watchpoint_remove(struct target *target, target_addr_t address)
{
	int retval = ERROR_OK;
	unsigned int num_found_watchpoints = 0;
	if (target->smp) {
		struct target_list *head;

		foreach_smp_target(head, target->smp_targets) {
			struct target *curr = head->target;
			int status = watchpoint_remove_internal(curr, address);

			if (status != ERROR_WATCHPOINT_NOT_FOUND) {
				num_found_watchpoints++;

				if (status != ERROR_OK) {
					LOG_TARGET_ERROR(curr, "failed to remove watchpoint at address " TARGET_ADDR_FMT, address);
					retval = status;
				}
			}
		}
	} else {
		retval = watchpoint_remove_internal(target, address);

		if (retval != ERROR_WATCHPOINT_NOT_FOUND) {
			num_found_watchpoints++;

			if (retval != ERROR_OK)
				LOG_TARGET_ERROR(target, "failed to remove watchpoint at address " TARGET_ADDR_FMT, address);
		}
	}

	if (num_found_watchpoints == 0) {
		LOG_TARGET_ERROR(target, "no watchpoint at address " TARGET_ADDR_FMT " found", address);
		return ERROR_WATCHPOINT_NOT_FOUND;
	}

	return retval;
}

int watchpoint_clear_target(struct target *target)
{
	LOG_DEBUG("Delete all watchpoints for target: %s",
		target_name(target));

	struct watchpoint *watchpoint = target->watchpoints;
	int retval = ERROR_OK;

	while (watchpoint) {
		struct watchpoint *tmp = watchpoint;
		watchpoint = watchpoint->next;
		int status = watchpoint_free(target, tmp);
		if (status != ERROR_OK)
			retval = status;
	}
	return retval;
}

int watchpoint_hit(struct target *target, enum watchpoint_rw *rw,
		   target_addr_t *address)
{
	int retval;
	struct watchpoint *hit_watchpoint;

	retval = target_hit_watchpoint(target, &hit_watchpoint);
	if (retval != ERROR_OK)
		return ERROR_FAIL;

	*rw = hit_watchpoint->rw;
	*address = hit_watchpoint->address;

	LOG_DEBUG("Found hit watchpoint at " TARGET_ADDR_FMT " (WPID: %d)",
		hit_watchpoint->address,
		hit_watchpoint->unique_id);

	return ERROR_OK;
}
